#include "services/LineupService.hpp"

#include "ServiceFixture.hpp"
#include "core/algorithms/Formations.hpp"
#include "core/domain/Permissions.hpp"
#include "data/readers/TacticJson.hpp"
#include "data/repositories/AvailabilityRepository.hpp"
#include "data/repositories/LineupRepository.hpp"
#include "data/repositories/MatchRepository.hpp"
#include "data/repositories/OpponentRepository.hpp"
#include "data/repositories/PlayerRepository.hpp"
#include "data/repositories/TrainingRepository.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace fdss::core;
using namespace fdss::data;
using namespace fdss::services;
using namespace fdss::tests;

namespace {

// FakeClock: today is Sunday 2026-11-01. The fixture has players 1 (Kowalski, cm, #7) and 2 (Nowak,
// st, #9) of team 1, opponent 1 ("Rywal") and planned match 1 on 2026-11-01.
constexpr MatchId kNextMatch{10};    // planned, Saturday 2026-11-07, "Liga", against opponent 1
constexpr MatchId kLaterMatch{11};   // planned, 2026-11-14
constexpr MatchId kFinishedMatch{5}; // 2026-10-10 against opponent 1, 2:1, "Liga"

class LineupServiceTest : public ServiceTest {
protected:
    void SetUp() override {
        ServiceTest::SetUp();
        if (HasFatalFailure()) {
            return;
        }
        seedCalendar();
        if (HasFatalFailure()) {
            return;
        }
        ASSERT_TRUE(sql("INSERT INTO opponent (id, name, name_key) VALUES (2, 'Inny', 'inny')"));
        ASSERT_TRUE(sql("INSERT INTO match (id, team_id, opponent_id, date, venue, competition) "
                        "VALUES (10, 1, 1, '2026-11-07', 'home', 'Liga'), "
                        "(11, 1, 2, '2026-11-14', 'away', 'Liga')"));
        ASSERT_TRUE(sql("INSERT INTO match (id, team_id, opponent_id, date, venue, competition, "
                        "goals_for, goals_against, status) "
                        "VALUES (5, 1, 1, '2026-10-10', 'away', 'Liga', 2, 1, 'finished')"));
        m_lineups = std::make_unique<LineupRepository>(*m_db);
        m_matches = std::make_unique<MatchRepository>(*m_db);
        m_opponents = std::make_unique<OpponentRepository>(*m_db);
        m_players = std::make_unique<PlayerRepository>(*m_db);
        m_availability = std::make_unique<AvailabilityRepository>(*m_db);
        m_training = std::make_unique<TrainingRepository>(*m_db);
        m_service =
            std::make_unique<LineupService>(*m_db, *m_lineups, *m_matches, *m_opponents, *m_players,
                                            *m_availability, *m_training, m_session, m_clock);
    }

    void logInAs(Role role) {
        const StaffUserId id = addUser(std::string(toDbText(role)).c_str(), role);
        logIn(id);
    }

    static Lineup lineup(const char* formation = "4-4-2") {
        Lineup result = emptyLineup(*findFormation(formation));
        result.pitchSlots[6].player = kPlayer;
        result.pitchSlots[9].player = kOtherPlayer;
        return result;
    }

    [[nodiscard]] MatchLineup read(MatchId match = kNextMatch, int threshold = 4) const {
        auto result = m_service->matchLineup(match, threshold);
        EXPECT_TRUE(result.has_value()) << result.error().detail;
        return result.value_or(MatchLineup{});
    }

    // A finished match with the given events (SQL: "(minute, type, player)").
    void finishedMatch(int id, const char* date, const char* competition, int opponent,
                       const char* events) {
        ASSERT_TRUE(sql("INSERT INTO match (id, team_id, opponent_id, date, venue, competition, "
                        "goals_for, goals_against, status) "
                        "VALUES (?, 1, ?, ?, 'home', ?, 0, 0, 'finished')",
                        {id, opponent, QString::fromUtf8(date),
                         competition ? QVariant(QString::fromUtf8(competition)) : QVariant()}));
        if (events) {
            ASSERT_TRUE(
                sql(std::string("INSERT INTO match_event (match_id, minute, type, player_id) "
                                "VALUES ")
                        .append(events)
                        .c_str()));
        }
    }

    // A session with saved attendance: player 1 and player 2 with the given statuses.
    void session(int id, const char* date, const char* first, const char* second) {
        ASSERT_TRUE(sql("INSERT INTO training_session (id, team_id, date, duration_min, type) "
                        "VALUES (?, 1, ?, 90, 'training')",
                        {id, QString::fromUtf8(date)}));
        ASSERT_TRUE(sql("INSERT INTO attendance (session_id, player_id, status) VALUES (?, 1, ?), "
                        "(?, 2, ?)",
                        {id, QString::fromUtf8(first), id, QString::fromUtf8(second)}));
    }

    std::unique_ptr<LineupRepository> m_lineups;
    std::unique_ptr<MatchRepository> m_matches;
    std::unique_ptr<OpponentRepository> m_opponents;
    std::unique_ptr<PlayerRepository> m_players;
    std::unique_ptr<AvailabilityRepository> m_availability;
    std::unique_ptr<TrainingRepository> m_training;
    std::unique_ptr<LineupService> m_service;
};

} // namespace

TEST_F(LineupServiceTest, ReadingNeedsALogin) {
    expectError(m_service->plannedMatches(kTeam), ErrorCode::PermissionDenied);
    expectError(m_service->matchLineup(kNextMatch, 4), ErrorCode::PermissionDenied);
    expectError(m_service->templates(), ErrorCode::PermissionDenied);
}

TEST_F(LineupServiceTest, PlannedMatchesAndTheNextOne) {
    logInAs(Role::Analyst);
    ASSERT_TRUE(sql("INSERT INTO match (id, team_id, opponent_id, date, venue) "
                    "VALUES (12, 1, 2, '2026-10-25', 'home')")); // planned, but in the past
    const auto planned = m_service->plannedMatches(kTeam);
    ASSERT_TRUE(planned.has_value());
    std::vector<std::int64_t> ids;
    for (const CalendarMatch& item : *planned) {
        ids.push_back(item.match.id.value);
    }
    // Match 1 of the fixture is today.
    EXPECT_EQ(ids, (std::vector<std::int64_t>{12, 1, 10, 11}));
    EXPECT_EQ(planned->at(2).opponentName, "Rywal");

    EXPECT_EQ(m_service->nextPlannedMatch(kTeam).value(), kMatch); // today counts
    m_clock.advance(std::chrono::days{2});
    EXPECT_EQ(m_service->nextPlannedMatch(kTeam).value(), kNextMatch);
    // After all of them: the last planned one.
    m_clock.advance(std::chrono::days{30});
    EXPECT_EQ(m_service->nextPlannedMatch(kTeam).value(), kLaterMatch);
    EXPECT_EQ(m_service->nextPlannedMatch(kOtherTeam).value(), std::nullopt);
}

TEST_F(LineupServiceTest, TheMatchAndTheLastMeeting) {
    logInAs(Role::Physio);
    finishedMatch(6, "2026-10-17", "Liga", 2, nullptr); // another opponent
    const MatchLineup result = read();
    EXPECT_EQ(result.match.match.id, kNextMatch);
    EXPECT_EQ(result.match.opponentName, "Rywal");
    ASSERT_TRUE(result.lastMeeting.has_value());
    EXPECT_EQ(result.lastMeeting->id, kFinishedMatch);
    EXPECT_EQ(result.lastMeeting->goalsFor, 2);
    const MatchLineup later = read(kLaterMatch);
    ASSERT_TRUE(later.lastMeeting.has_value());
    EXPECT_EQ(later.lastMeeting->id, MatchId{6});
}

TEST_F(LineupServiceTest, NothingSavedGivesAnEmptyShape) {
    logInAs(Role::Coach);
    MatchLineup result = read();
    EXPECT_FALSE(result.saved);
    EXPECT_EQ(result.lineup, emptyLineup(*findFormation("4-4-2")));

    ASSERT_TRUE(sql("UPDATE match SET formation = '3-5-2' WHERE id = 10"));
    result = read();
    EXPECT_EQ(result.lineup, emptyLineup(*findFormation("3-5-2")));
}

TEST_F(LineupServiceTest, StatusOnTheMatchDate) {
    logInAs(Role::Coach);
    // Injured since Oct 20, no return yet: still injured on Nov 7.
    ASSERT_TRUE(sql("INSERT INTO availability (player_id, type, from_date, expected_to) "
                    "VALUES (1, 'injury', '2026-10-20', '2026-11-03')"));
    // Suspended through Nov 5: available again on Nov 7, suspended on Nov 1.
    ASSERT_TRUE(sql("INSERT INTO availability (player_id, type, from_date, expected_to) "
                    "VALUES (2, 'suspension', '2026-10-30', '2026-11-05')"));
    MatchLineup result = read();
    ASSERT_EQ(result.candidates.size(), 2U);
    EXPECT_EQ(result.context.players.at(kPlayer).statusOnMatchDay, PlayerStatus::Injured);
    EXPECT_EQ(result.context.players.at(kOtherPlayer).statusOnMatchDay, PlayerStatus::Available);
    EXPECT_EQ(result.context.players.at(kPlayer).primaryPosition, Position::Cm);
    EXPECT_EQ(read(kMatch).context.players.at(kOtherPlayer).statusOnMatchDay,
              PlayerStatus::Suspended);
}

// P12 decision 3: this team (season) and competition, before the match, without matches with a red.
TEST_F(LineupServiceTest, YellowCardsOfThisCompetitionAndSeason) {
    logInAs(Role::Coach);
    ASSERT_TRUE(sql("INSERT INTO match_event (match_id, minute, type, player_id) "
                    "VALUES (5, 30, 'yellow', 2), (5, 31, 'yellow', 1)"));
    finishedMatch(7, "2026-10-24", "Liga", 2, "(7, 10, 'yellow', 2)");
    finishedMatch(8, "2026-10-28", "Puchar", 2, "(8, 10, 'yellow', 2)"); // cup
    finishedMatch(9, "2026-10-31", "Liga", 2,
                  "(9, 10, 'yellow', 2), (9, 50, 'yellow', 1), "
                  "(9, 80, 'yellow', 1), (9, 80, 'red', 1)");            // sent off
    finishedMatch(13, "2026-11-08", "Liga", 2, "(13, 10, 'yellow', 2)"); // after
    ASSERT_TRUE(sql("INSERT INTO team (id, club_id, name, category, season) "
                    "VALUES (3, 1, 'Seniorzy', 'senior', '2025/26')"));
    ASSERT_TRUE(sql("INSERT INTO match (id, team_id, opponent_id, date, venue, competition, "
                    "goals_for, goals_against, status) "
                    "VALUES (14, 3, 1, '2026-05-01', 'home', 'Liga', 0, 0, 'finished')"));
    ASSERT_TRUE(sql("INSERT INTO match_event (match_id, minute, type, player_id) "
                    "VALUES (14, 10, 'yellow', 2)")); // last season

    const MatchLineup result = read();
    EXPECT_EQ(result.context.players.at(kOtherPlayer).yellowCards, 3);
    EXPECT_EQ(result.context.players.at(kPlayer).yellowCards, 1);
    EXPECT_EQ(result.context.yellowCardThreshold, 4);
    EXPECT_EQ(read(kNextMatch, 6).context.yellowCardThreshold, 6);

    // The validator sees it: 3 cards at threshold 4.
    Lineup board = lineup();
    const auto issues = m_service->check(board, result.context);
    EXPECT_TRUE(std::ranges::any_of(issues, [](const LineupIssue& issue) {
        return issue.rule == LineupRule::YellowCards && issue.player == kOtherPlayer;
    }));
}

TEST_F(LineupServiceTest, CardsWithoutACompetitionCountWithoutACompetition) {
    logInAs(Role::Coach);
    ASSERT_TRUE(sql("UPDATE match SET competition = NULL WHERE id = 10"));
    finishedMatch(7, "2026-10-24", nullptr, 2, "(7, 10, 'yellow', 2)");
    EXPECT_EQ(read().context.players.at(kOtherPlayer).yellowCards, 1);
}

// P12 decision 8.
TEST_F(LineupServiceTest, MissedTheLastThreeSessionsWithAttendance) {
    logInAs(Role::Coach);
    session(20, "2026-10-26", "present", "present");
    session(21, "2026-10-28", "absent", "absent");
    session(22, "2026-10-29", "excused", "present");
    session(23, "2026-10-30", "injured", "absent");
    // Without attendance: not counted.
    ASSERT_TRUE(sql("INSERT INTO training_session (id, team_id, date, duration_min, type) "
                    "VALUES (24, 1, '2026-10-31', 90, 'training')"));
    MatchLineup result = read();
    EXPECT_TRUE(result.context.players.at(kPlayer).missedRecentSessions);
    EXPECT_FALSE(result.context.players.at(kOtherPlayer).missedRecentSessions);

    // A session after today is not counted either, even before the match.
    m_clock.advance(std::chrono::days{3});
    session(25, "2026-11-03", "present", "absent");
    m_clock.advance(-std::chrono::days{3});
    EXPECT_TRUE(read().context.players.at(kPlayer).missedRecentSessions);
    m_clock.advance(std::chrono::days{3});
    EXPECT_FALSE(read().context.players.at(kPlayer).missedRecentSessions);
}

TEST_F(LineupServiceTest, FewerThanThreeSessionsWarnNobody) {
    logInAs(Role::Coach);
    session(20, "2026-10-28", "absent", "absent");
    session(21, "2026-10-29", "absent", "absent");
    EXPECT_FALSE(read().context.players.at(kPlayer).missedRecentSessions);
}

TEST_F(LineupServiceTest, SaveAndReadAgain) {
    logInAs(Role::Assistant);
    Lineup board = lineup();
    ASSERT_EQ(moveSlot(board, 8, Position::Lw), LineupEdit::Done);
    board.formation = "a name the board made up";
    ASSERT_TRUE(m_service->save(kNextMatch, board).has_value());
    const MatchLineup result = read();
    EXPECT_TRUE(result.saved);
    board.formation = "4-3-1-2";
    EXPECT_EQ(result.lineup, board);
    EXPECT_EQ(result.droppedPlayers, 0);
    EXPECT_EQ(m_matches->get(kNextMatch).value().formation, "4-3-1-2");
}

TEST_F(LineupServiceTest, SavingWithErrorsIsADraft) {
    logInAs(Role::Coach);
    ASSERT_TRUE(sql("INSERT INTO availability (player_id, type, from_date) "
                    "VALUES (1, 'injury', '2026-10-20')"));
    const Lineup board = lineup();
    const MatchLineup before = read();
    EXPECT_TRUE(hasErrors(m_service->check(board, before.context)));
    EXPECT_TRUE(m_service->save(kNextMatch, board).has_value());
}

TEST_F(LineupServiceTest, SaveRejectsAWrongShapeAndAMatchThatIsNotPlanned) {
    logInAs(Role::Coach);
    Lineup board = lineup();
    board.pitchSlots.pop_back();
    expectError(m_service->save(kNextMatch, board), ErrorCode::InvalidInput, "lineup_shape");
    board = lineup();
    board.pitchSlots[3].position = Position::Gk;
    expectError(m_service->save(kNextMatch, board), ErrorCode::InvalidInput, "lineup_shape");
    board = lineup();
    board.pitchSlots[3].x = 1.5;
    expectError(m_service->save(kNextMatch, board), ErrorCode::InvalidInput, "lineup_shape");
    board = lineup();
    for (std::int64_t id = 30; id < 43; ++id) {
        board.bench.push_back(PlayerId{id});
    }
    expectError(m_service->save(kNextMatch, board), ErrorCode::InvalidInput, "lineup_shape");

    expectError(m_service->save(kFinishedMatch, lineup()), ErrorCode::Conflict,
                "match_not_planned");
    expectError(m_service->save(MatchId{999}, lineup()), ErrorCode::NotFound);
}

TEST_F(LineupServiceTest, PlayersNoLongerInTheSquadAreDropped) {
    logInAs(Role::Coach);
    Lineup board = lineup();
    board.bench = {PlayerId{1}}; // a duplicate stays: the validator reports it
    ASSERT_TRUE(m_service->save(kNextMatch, board).has_value());
    ASSERT_TRUE(sql("UPDATE player SET archived_at = '2026-11-01' WHERE id = 2"));
    const MatchLineup result = read();
    EXPECT_EQ(result.droppedPlayers, 1);
    EXPECT_EQ(result.lineup.pitchSlots[9].player, std::nullopt);
    EXPECT_EQ(result.lineup.pitchSlots[6].player, kPlayer);
    EXPECT_EQ(result.candidates.size(), 1U);
}

TEST_F(LineupServiceTest, Templates) {
    logInAs(Role::Coach);
    const auto first = m_service->saveTemplate("  Plan A ", lineup(), false);
    ASSERT_TRUE(first.has_value()) << first.error().detail;
    expectError(m_service->saveTemplate("plan a", lineup("3-5-2"), false), ErrorCode::Conflict,
                "template_name");
    const auto replaced = m_service->saveTemplate("PLAN A", lineup("3-5-2"), true);
    ASSERT_TRUE(replaced.has_value());
    EXPECT_EQ(*replaced, *first);
    EXPECT_EQ(m_service->templates().value(),
              (std::vector<LineupTemplateInfo>{{*first, "Plan A", "3-5-2"}}));

    const auto loaded = m_service->loadTemplate(*first, kTeam);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->lineup, lineup("3-5-2"));
    EXPECT_EQ(loaded->droppedPlayers, 0);
    // In another team nobody of the template is in the squad.
    EXPECT_EQ(m_service->loadTemplate(*first, kOtherTeam).value().droppedPlayers, 2);
}

TEST_F(LineupServiceTest, TemplateNames) {
    logInAs(Role::Coach);
    expectError(m_service->saveTemplate("   ", lineup(), false), ErrorCode::InvalidInput,
                "template_name");
    expectError(m_service->saveTemplate(std::string(51, 'x'), lineup(), false),
                ErrorCode::InvalidInput, "template_name");
    std::string fifty;
    for (int i = 0; i < 50; ++i) {
        fifty += "ą"; // 50 characters, 100 bytes
    }
    EXPECT_TRUE(m_service->saveTemplate(fifty, lineup(), false).has_value());
    // Case folding beyond ASCII.
    EXPECT_TRUE(m_service->saveTemplate("Łódź", lineup(), false).has_value());
    expectError(m_service->saveTemplate("łÓDŹ", lineup(), false), ErrorCode::Conflict,
                "template_name");
}

TEST_F(LineupServiceTest, DeleteAndRestoreATemplate) {
    logInAs(Role::Coach);
    const auto id = m_service->saveTemplate("Plan A", lineup("4-3-3"), false);
    ASSERT_TRUE(id.has_value());
    const auto deleted = m_service->deleteTemplate(*id);
    ASSERT_TRUE(deleted.has_value());
    EXPECT_EQ(deleted->name, "Plan A");
    EXPECT_TRUE(m_service->templates().value().empty());
    expectError(m_service->deleteTemplate(*id), ErrorCode::NotFound);

    const auto restored = m_service->restoreTemplate(*deleted);
    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(m_service->loadTemplate(*restored, kTeam).value().lineup, lineup("4-3-3"));
}

TEST_F(LineupServiceTest, ExportAndImportAFile) {
    logInAs(Role::Coach);
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath("plan.tactic.json");
    Lineup board = lineup();
    board.bench = {PlayerId{99}}; // not in the squad
    ASSERT_TRUE(m_service->exportTactic(path, board).has_value());
    const auto imported = m_service->importTactic(path, kTeam);
    ASSERT_TRUE(imported.has_value()) << imported.error().detail;
    EXPECT_EQ(imported->lineup, lineup());
    EXPECT_EQ(imported->droppedPlayers, 1);

    QFile broken(dir.filePath("broken.tactic.json"));
    ASSERT_TRUE(broken.open(QIODevice::WriteOnly));
    broken.write("{\"version\": 1, \"slots\": [");
    broken.close();
    expectError(m_service->importTactic(broken.fileName(), kTeam), ErrorCode::UnsupportedFormat,
                "tactic_file");
    // Nothing reached the database.
    EXPECT_EQ(count("SELECT COUNT(*) FROM lineup"), 0);
}

TEST_F(LineupServiceTest, PermissionPerRole) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath("plan.tactic.json");
    ASSERT_TRUE(writeTacticFile(path, lineup()).has_value());
    int templateNumber = 0;
    for (const Role role : allValues<Role>()) {
        SCOPED_TRACE(std::string(toDbText(role)));
        logInAs(role);
        const bool allowed = isAllowed(role, Permission::EditLineup);
        // Reading and exporting: every role.
        EXPECT_TRUE(m_service->matchLineup(kNextMatch, 4).has_value());
        EXPECT_TRUE(m_service->templates().has_value());
        EXPECT_TRUE(m_service->exportTactic(dir.filePath("out.tactic.json"), lineup()).has_value());
        // The rest: EditLineup (Coach and Assistant).
        const std::string name = std::format("Plan {}", ++templateNumber);
        const auto saved = m_service->saveTemplate(name, lineup(), false);
        EXPECT_EQ(saved.has_value(), allowed);
        EXPECT_EQ(m_service->save(kNextMatch, lineup()).has_value(), allowed);
        EXPECT_EQ(m_service->importTactic(path, kTeam).has_value(), allowed);
        if (!allowed) {
            EXPECT_EQ(saved.error().code, ErrorCode::PermissionDenied);
            const auto existing = m_service->templates().value();
            ASSERT_FALSE(existing.empty());
            expectError(m_service->loadTemplate(existing.front().id, kTeam),
                        ErrorCode::PermissionDenied);
            expectError(m_service->deleteTemplate(existing.front().id),
                        ErrorCode::PermissionDenied);
        }
    }
    EXPECT_TRUE(isAllowed(Role::Coach, Permission::EditLineup));
    EXPECT_TRUE(isAllowed(Role::Assistant, Permission::EditLineup));
    EXPECT_FALSE(isAllowed(Role::Physio, Permission::EditLineup));
    EXPECT_FALSE(isAllowed(Role::Analyst, Permission::EditLineup));
}
