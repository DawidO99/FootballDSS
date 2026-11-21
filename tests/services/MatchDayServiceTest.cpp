#include "services/MatchDayService.hpp"

#include "ServiceFixture.hpp"
#include "core/algorithms/Formations.hpp"
#include "data/repositories/AvailabilityRepository.hpp"
#include "data/repositories/ClubRepository.hpp"
#include "data/repositories/LineupRepository.hpp"
#include "data/repositories/MatchLiveRepository.hpp"
#include "data/repositories/MatchNoteRepository.hpp"
#include "data/repositories/MatchRepository.hpp"
#include "data/repositories/MatchTacticRepository.hpp"
#include "data/repositories/OpponentRepository.hpp"
#include "data/repositories/PlayerRepository.hpp"
#include "data/repositories/StaffRepository.hpp"
#include "data/repositories/TrainingRepository.hpp"
#include "services/AuthService.hpp"
#include "services/LineupService.hpp"
#include "services/Session.hpp"

#include <QSqlQuery>
#include <QSqlRecord>
#include <QStringList>

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>

using namespace fdss::core;
using namespace fdss::data;
using namespace fdss::services;
using namespace fdss::tests;
using namespace std::chrono_literals;

namespace {

// The fixture: team 1 (senior) with players 1–14; planned match 1 today (FakeClock: 2026-11-01).
// The saved lineup of match 1 (4-4-2): goalkeeper 3 in slot 0, then 1, 2, 4, 5, …, 11; bench 12,
// 13, 14.
constexpr MatchId kFinished{5};
constexpr MatchId kOtherPlanned{6};
constexpr MatchId kMatchId{1};
constexpr MatchFormat kHalves{2, 45};

PlayerId pid(std::int64_t id) { return PlayerId{id}; }

class MatchDayServiceTest : public ServiceTest {
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
        for (int id = 3; id <= 14; ++id) {
            ASSERT_TRUE(sql("INSERT INTO player (id, team_id, first_name, last_name, "
                            "shirt_number, primary_position) VALUES (?, 1, 'P', ?, ?, ?)",
                            {id, QStringLiteral("Player%1").arg(id), id + 20,
                             id == 3 ? QStringLiteral("gk") : QStringLiteral("cm")}));
        }
        ASSERT_TRUE(sql("INSERT INTO match (id, team_id, opponent_id, date, venue, goals_for, "
                        "goals_against, status) VALUES (5, 1, 1, '2026-10-10', 'home', 1, 0, "
                        "'finished'), (6, 1, 1, '2026-11-08', 'home', NULL, NULL, 'planned')"));
        m_clubs = std::make_unique<ClubRepository>(*m_db);
        m_lineups = std::make_unique<LineupRepository>(*m_db);
        m_matches = std::make_unique<MatchRepository>(*m_db);
        m_live = std::make_unique<MatchLiveRepository>(*m_db);
        m_notes = std::make_unique<MatchNoteRepository>(*m_db);
        m_tactics = std::make_unique<MatchTacticRepository>(*m_db);
        m_opponents = std::make_unique<OpponentRepository>(*m_db);
        m_players = std::make_unique<PlayerRepository>(*m_db);
        m_availability = std::make_unique<AvailabilityRepository>(*m_db);
        m_training = std::make_unique<TrainingRepository>(*m_db);
        m_lineupService =
            std::make_unique<LineupService>(*m_db, *m_lineups, *m_matches, *m_opponents, *m_players,
                                            *m_availability, *m_training, m_session, m_clock);
        restart();
    }

    // A new service on the same database and clock, as after closing and opening the app.
    void restart() {
        m_service = std::make_unique<MatchDayService>(
            *m_db, *m_matches, *m_live, *m_notes, *m_tactics, *m_lineups, *m_clubs, *m_opponents,
            *m_players, *m_lineupService, m_session, m_clock);
    }

    void logInAs(Role role) { logIn(addUser(std::string(toDbText(role)).c_str(), role)); }

    static Lineup lineup(bool withGoalkeeper = true) {
        Lineup result = emptyLineup(*findFormation("4-4-2"));
        const std::int64_t players[] = {3, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11};
        for (std::size_t slot = 0; slot < result.pitchSlots.size(); ++slot) {
            result.pitchSlots[slot].player = pid(players[slot]);
        }
        if (!withGoalkeeper) {
            result.pitchSlots[0].player.reset();
        }
        result.bench = {pid(12), pid(13), pid(14)};
        return result;
    }

    void saveLineup(MatchId match = kMatch, bool withGoalkeeper = true) {
        ASSERT_TRUE(m_lineups->saveForMatch(match, lineup(withGoalkeeper)));
    }

    // Coach logged in, lineup saved, Match Day started with 45-minute halves.
    void startMatch() {
        logInAs(Role::Coach);
        saveLineup();
        const auto started = m_service->start(kMatch, kHalves, 4);
        ASSERT_TRUE(started.has_value()) << started.error().detail;
    }

    void advance() {
        const auto advanced = m_service->advanceClock(kMatch);
        ASSERT_TRUE(advanced.has_value()) << advanced.error().detail;
    }

    [[nodiscard]] MatchDayState read() const {
        auto state = m_service->state(kMatch);
        EXPECT_TRUE(state.has_value()) << state.error().detail;
        return state.value_or(MatchDayState{});
    }

    [[nodiscard]] Appearance appearance(std::int64_t player) const {
        const auto appearances = m_matches->listAppearances(kMatch);
        EXPECT_TRUE(appearances.has_value());
        for (const Appearance& item : appearances.value_or(std::vector<Appearance>{})) {
            if (item.playerId == pid(player)) {
                return item;
            }
        }
        ADD_FAILURE() << "no appearance of player " << player;
        return {};
    }

    void expectMinutes(std::int64_t player, std::optional<int> on, std::optional<int> off) const {
        SCOPED_TRACE(player);
        const Appearance item = appearance(player);
        EXPECT_EQ(item.minuteOn, on);
        EXPECT_EQ(item.minuteOff, off);
    }

    [[nodiscard]] int eventCount() const { return count("SELECT count(*) FROM match_event"); }

    std::unique_ptr<ClubRepository> m_clubs;
    std::unique_ptr<LineupRepository> m_lineups;
    std::unique_ptr<MatchRepository> m_matches;
    std::unique_ptr<MatchLiveRepository> m_live;
    std::unique_ptr<MatchNoteRepository> m_notes;
    std::unique_ptr<MatchTacticRepository> m_tactics;
    std::unique_ptr<OpponentRepository> m_opponents;
    std::unique_ptr<PlayerRepository> m_players;
    std::unique_ptr<AvailabilityRepository> m_availability;
    std::unique_ptr<TrainingRepository> m_training;
    std::unique_ptr<LineupService> m_lineupService;
    std::unique_ptr<MatchDayService> m_service;
};

} // namespace

TEST_F(MatchDayServiceTest, ReadingNeedsALogin) {
    expectError(m_service->state(kMatch), ErrorCode::PermissionDenied);
    expectError(m_service->liveMatch(kTeam), ErrorCode::PermissionDenied);
    expectError(m_service->startCheck(kMatch, 4), ErrorCode::PermissionDenied);
}

TEST_F(MatchDayServiceTest, ActionsNeedRunMatchDay) {
    saveLineup();
    for (const Role role : {Role::Physio, Role::Analyst}) {
        SCOPED_TRACE(toDbText(role));
        logInAs(role);
        // Every role reads.
        EXPECT_TRUE(m_service->state(kMatch).has_value());
        expectError(m_service->start(kMatch, kHalves, 4), ErrorCode::PermissionDenied);
        expectError(m_service->cancelStart(kMatch), ErrorCode::PermissionDenied);
        expectError(m_service->advanceClock(kMatch), ErrorCode::PermissionDenied);
        expectError(m_service->recordGoal(kMatch, pid(9), std::nullopt),
                    ErrorCode::PermissionDenied);
        expectError(m_service->recordOpponentGoal(kMatch), ErrorCode::PermissionDenied);
        expectError(m_service->recordYellow(kMatch, pid(9)), ErrorCode::PermissionDenied);
        expectError(m_service->recordRed(kMatch, pid(9)), ErrorCode::PermissionDenied);
        expectError(m_service->recordSubstitution(kMatch, pid(12), pid(9)),
                    ErrorCode::PermissionDenied);
        expectError(m_service->undoLast(kMatch), ErrorCode::PermissionDenied);
    }
    for (const Role role : {Role::Assistant, Role::Coach}) {
        SCOPED_TRACE(toDbText(role));
        logInAs(role);
        ASSERT_TRUE(m_service->start(kMatch, kHalves, 4).has_value());
        ASSERT_TRUE(m_service->cancelStart(kMatch).has_value());
    }
}

TEST_F(MatchDayServiceTest, StartCheckNamesWhatBlocksTheStart) {
    logInAs(Role::Coach);
    auto check = m_service->startCheck(kMatch, 4);
    ASSERT_TRUE(check.has_value()) << check.error().detail;
    EXPECT_EQ(check->block, MatchDayStartCheck::Block::NoLineup);
    EXPECT_EQ(check->defaultFormat, (MatchFormat{2, 45}));
    expectError(m_service->start(kMatch, kHalves, 4), ErrorCode::Conflict, "no_lineup");

    saveLineup(kMatch, false);
    check = m_service->startCheck(kMatch, 4);
    EXPECT_EQ(check->block, MatchDayStartCheck::Block::LineupErrors);
    EXPECT_GT(check->lineupErrors, 0);
    expectError(m_service->start(kMatch, kHalves, 4), ErrorCode::Conflict, "lineup_errors");

    saveLineup();
    check = m_service->startCheck(kMatch, 4);
    EXPECT_EQ(check->block, MatchDayStartCheck::Block::None);
    EXPECT_EQ(check->lineupErrors, 0);

    EXPECT_EQ(m_service->startCheck(kFinished, 4)->block, MatchDayStartCheck::Block::NotPlanned);
    expectError(m_service->start(kFinished, kHalves, 4), ErrorCode::Conflict, "match_not_planned");

    expectError(m_service->start(kMatch, {2, 9}, 4), ErrorCode::InvalidInput, "match_format");
    expectError(m_service->start(kMatch, {5, 20}, 4), ErrorCode::InvalidInput, "match_format");
    expectError(m_service->start(kMatch, {4, 40}, 4), ErrorCode::InvalidInput, "match_format");
    ASSERT_TRUE(m_service->start(kMatch, kHalves, 4).has_value());
    ASSERT_TRUE(m_lineups->saveForMatch(kOtherPlanned, lineup()));
    EXPECT_EQ(m_service->startCheck(kOtherPlanned, 4)->block,
              MatchDayStartCheck::Block::AnotherLive);
    expectError(m_service->start(kOtherPlanned, kHalves, 4), ErrorCode::Conflict,
                "another_live_match");
}

TEST_F(MatchDayServiceTest, DefaultHalfLengthFollowsTheCategory) {
    logInAs(Role::Coach);
    ASSERT_TRUE(sql("UPDATE team SET category = 'u13' WHERE id = 1"));
    EXPECT_EQ(m_service->startCheck(kMatch, 4)->defaultFormat, (MatchFormat{2, 30}));
}

TEST_F(MatchDayServiceTest, StartMakesTheMatchLiveWithStartersOnThePitch) {
    startMatch();
    EXPECT_EQ(m_matches->get(kMatch)->status, MatchStatus::Live);
    EXPECT_EQ(m_service->liveMatch(kTeam).value(), kMatch);
    const MatchDayState state = read();
    ASSERT_TRUE(state.clock.has_value());
    EXPECT_EQ(state.clock->format, kHalves);
    EXPECT_EQ(phaseOf(*state.clock), MatchPhase::NotStarted);
    EXPECT_EQ(state.players.size(), 14U);
    EXPECT_EQ(state.lastAction.has_value(), false);
    EXPECT_EQ(count("SELECT count(*) FROM appearance"), 11);
    expectMinutes(3, 0, std::nullopt);
    // Events need a running half.
    expectError(m_service->recordGoal(kMatch, pid(9), std::nullopt), ErrorCode::Conflict,
                "match_not_running");
}

TEST_F(MatchDayServiceTest, BackToPlannedOnlyBeforeTheKickOff) {
    startMatch();
    ASSERT_TRUE(m_service->cancelStart(kMatch).has_value());
    EXPECT_EQ(m_matches->get(kMatch)->status, MatchStatus::Planned);
    EXPECT_EQ(count("SELECT count(*) FROM appearance"), 0);
    EXPECT_EQ(count("SELECT count(*) FROM match_live"), 0);

    ASSERT_TRUE(m_service->start(kMatch, kHalves, 4).has_value());
    advance();
    expectError(m_service->cancelStart(kMatch), ErrorCode::Conflict, "match_started");
}

// The "Done when" of the packet: 2 goals, a substitution, a yellow, a second yellow, the end;
// minutes counted by hand.
TEST_F(MatchDayServiceTest, FullMatchGivesTheMinutesCountedByHand) {
    startMatch();
    advance(); // kick-off at 12:00
    m_clock.advance(10min + 20s);
    ASSERT_TRUE(m_service->recordGoal(kMatch, pid(9), pid(10)).has_value()); // 11', stored 10
    m_clock.advance(20min);
    ASSERT_TRUE(m_service->recordYellow(kMatch, pid(4)).has_value()); // 31', stored 30
    m_clock.advance(5min);
    ASSERT_TRUE(m_service->recordOpponentGoal(kMatch).has_value());               // 36', stored 35
    m_clock.advance(11min + 50s);                                                 // 47:10
    ASSERT_TRUE(m_service->recordGoal(kMatch, pid(2), std::nullopt).has_value()); // 45+3'
    m_clock.advance(50s);
    advance(); // half time at 48:00
    m_clock.advance(10min);
    // A substitution in the break: minute 45.
    ASSERT_TRUE(m_service->recordSubstitution(kMatch, pid(12), pid(5)).has_value());
    // Nothing in the break at the minute that starts the second half, except a substitution.
    expectError(m_service->recordYellow(kMatch, pid(6), MatchTime{45, std::nullopt}),
                ErrorCode::InvalidInput, "minute_not_played");
    m_clock.advance(5min);
    advance(); // second half
    m_clock.advance(15min);
    ASSERT_TRUE(m_service->recordYellow(kMatch, pid(4)).has_value()); // second yellow at 60
    m_clock.advance(20min);
    ASSERT_TRUE(m_service->recordGoal(kMatch, pid(12), std::nullopt).has_value()); // stored 80
    m_clock.advance(14min + 30s); // 49:30 into the half: 90+5
    advance();                    // the end

    const auto events = m_matches->listEvents(kMatch);
    ASSERT_TRUE(events.has_value());
    ASSERT_EQ(events->size(), 8U);
    const auto at = [&](std::size_t index) {
        return std::pair{events->at(index).type,
                         MatchTime{events->at(index).minute, events->at(index).addedTime}};
    };
    EXPECT_EQ(at(0), (std::pair{MatchEventType::Goal, MatchTime{10, std::nullopt}}));
    EXPECT_EQ(events->at(0).relatedPlayerId, pid(10));
    EXPECT_EQ(at(1), (std::pair{MatchEventType::Yellow, MatchTime{30, std::nullopt}}));
    EXPECT_EQ(at(2), (std::pair{MatchEventType::OpponentGoal, MatchTime{35, std::nullopt}}));
    EXPECT_EQ(at(3), (std::pair{MatchEventType::Goal, MatchTime{45, 3}}));
    EXPECT_EQ(at(4), (std::pair{MatchEventType::Sub, MatchTime{45, std::nullopt}}));
    EXPECT_EQ(at(5), (std::pair{MatchEventType::Yellow, MatchTime{60, std::nullopt}}));
    EXPECT_EQ(at(6), (std::pair{MatchEventType::Red, MatchTime{60, std::nullopt}}));
    EXPECT_EQ(events->at(6).relatedPlayerId, std::nullopt);
    EXPECT_EQ(at(7), (std::pair{MatchEventType::Goal, MatchTime{80, std::nullopt}}));

    const auto match = m_matches->get(kMatch);
    EXPECT_EQ(match->status, MatchStatus::Finished);
    EXPECT_EQ(match->goalsFor, 3);
    EXPECT_EQ(match->goalsAgainst, 1);
    // 12 appearances: the XI and the substitute.
    EXPECT_EQ(count("SELECT count(*) FROM appearance"), 12);
    expectMinutes(5, 0, 45);   // off at half time: 45 minutes
    expectMinutes(12, 45, 90); // 45 minutes
    expectMinutes(4, 0, 60);   // sent off at 60
    expectMinutes(9, 0, 90);   // the whole match is 90, whatever the stoppage time
    EXPECT_EQ(appearance(12).position, Position::Lb);
    EXPECT_EQ(count("SELECT sum(minute_off - minute_on) FROM appearance"), 9 * 90 + 60 + 45 + 45);

    // Nothing more once the match is finished.
    expectError(m_service->recordGoal(kMatch, pid(9), std::nullopt), ErrorCode::Conflict,
                "match_not_live");
    expectError(m_service->advanceClock(kMatch), ErrorCode::Conflict, "match_not_live");
}

TEST_F(MatchDayServiceTest, AnInvalidEventWritesNothing) {
    startMatch();
    advance();
    m_clock.advance(10min);
    ASSERT_TRUE(m_service->recordSubstitution(kMatch, pid(12), pid(5)).has_value());
    const int events = eventCount();
    expectError(m_service->recordSubstitution(kMatch, pid(13), pid(5)), ErrorCode::InvalidInput,
                "player_substituted");
    expectError(m_service->recordSubstitution(kMatch, pid(5), pid(9)), ErrorCode::InvalidInput,
                "player_substituted");
    expectError(m_service->recordSubstitution(kMatch, pid(9), pid(10)), ErrorCode::InvalidInput,
                "player_not_on_bench");
    expectError(m_service->recordGoal(kMatch, pid(9), pid(9)), ErrorCode::InvalidInput,
                "same_player");
    expectError(m_service->recordGoal(kMatch, pid(13), std::nullopt), ErrorCode::InvalidInput,
                "player_not_on_pitch");
    expectError(m_service->recordYellow(kMatch, pid(99)), ErrorCode::InvalidInput,
                "player_unknown");
    ASSERT_TRUE(m_service->recordRed(kMatch, pid(7)).has_value());
    expectError(m_service->recordSubstitution(kMatch, pid(13), pid(7)), ErrorCode::InvalidInput,
                "player_sent_off");
    EXPECT_EQ(eventCount(), events + 1);
    expectMinutes(5, 0, 10);
    expectMinutes(12, 10, std::nullopt);
    expectMinutes(7, 0, 10);
}

TEST_F(MatchDayServiceTest, UndoTakesBackEventsAndClockStepsInOrder) {
    startMatch();
    expectError(m_service->undoLast(kMatch), ErrorCode::NotFound, "nothing_to_undo");
    advance();
    m_clock.advance(5min);
    ASSERT_TRUE(m_service->recordYellow(kMatch, pid(4)).has_value());
    m_clock.advance(40min);
    advance(); // half time; the last action is the clock step
    ASSERT_EQ(read().lastAction->kind, MatchDayAction::Kind::ClockStep);

    auto undone = m_service->undoLast(kMatch);
    ASSERT_TRUE(undone.has_value()) << undone.error().detail;
    EXPECT_EQ(undone->kind, MatchDayAction::Kind::ClockStep);
    EXPECT_EQ(undone->step, (ClockStep{1, ClockStepKind::End}));
    EXPECT_EQ(phaseOf(*read().clock), MatchPhase::Running);

    m_clock.advance(1min);
    ASSERT_TRUE(m_service->recordYellow(kMatch, pid(4)).has_value()); // second yellow: two rows
    EXPECT_EQ(eventCount(), 3);
    expectMinutes(4, 0, 45); // 45+2, cut at the end of the half
    undone = m_service->undoLast(kMatch);
    ASSERT_TRUE(undone.has_value());
    EXPECT_EQ(undone->kind, MatchDayAction::Kind::SecondYellow);
    EXPECT_EQ(undone->event->playerId, pid(4));
    EXPECT_EQ(eventCount(), 1);
    expectMinutes(4, 0, std::nullopt);

    undone = m_service->undoLast(kMatch);
    ASSERT_TRUE(undone.has_value());
    EXPECT_EQ(undone->kind, MatchDayAction::Kind::Event);
    EXPECT_EQ(undone->event->type, MatchEventType::Yellow);
    EXPECT_EQ(eventCount(), 0);

    undone = m_service->undoLast(kMatch);
    ASSERT_TRUE(undone.has_value());
    EXPECT_EQ(undone->step, (ClockStep{1, ClockStepKind::Start}));
    EXPECT_EQ(phaseOf(*read().clock), MatchPhase::NotStarted);
    expectError(m_service->undoLast(kMatch), ErrorCode::NotFound, "nothing_to_undo");
    // Still live: back to planned is a separate action.
    EXPECT_EQ(m_matches->get(kMatch)->status, MatchStatus::Live);
}

TEST_F(MatchDayServiceTest, UndoOfTheEndMakesTheMatchLiveAgain) {
    startMatch();
    advance();
    m_clock.advance(10min);
    ASSERT_TRUE(m_service->recordGoal(kMatch, pid(9), std::nullopt).has_value());
    m_clock.advance(36min);
    advance();
    m_clock.advance(15min);
    advance();
    m_clock.advance(46min);
    advance();
    ASSERT_EQ(m_matches->get(kMatch)->goalsFor, 1);
    expectMinutes(9, 0, 90);

    const auto undone = m_service->undoLast(kMatch);
    ASSERT_TRUE(undone.has_value()) << undone.error().detail;
    EXPECT_EQ(undone->kind, MatchDayAction::Kind::Finish);
    EXPECT_EQ(undone->step, (ClockStep{2, ClockStepKind::End}));
    const auto match = m_matches->get(kMatch);
    EXPECT_EQ(match->status, MatchStatus::Live);
    EXPECT_EQ(match->goalsFor, std::nullopt);
    EXPECT_EQ(match->goalsAgainst, std::nullopt);
    expectMinutes(9, 0, std::nullopt);
    EXPECT_EQ(phaseOf(*read().clock), MatchPhase::Running);
    EXPECT_EQ(currentPeriod(*read().clock), 2);
}

TEST_F(MatchDayServiceTest, ResumesAfterARestartWhileTimePassed) {
    startMatch();
    advance();
    m_clock.advance(20min); // the app closes at 20:00
    m_service.reset();
    m_clock.advance(5min); // and opens 5 minutes later
    restart();
    const MatchDayState state = read();
    ASSERT_TRUE(state.clock.has_value());
    const ClockReading reading = readClock(*state.clock, m_clock.now());
    EXPECT_EQ(reading.phase, MatchPhase::Running);
    EXPECT_EQ(reading.regular, 25min);
    ASSERT_TRUE(m_service->recordGoal(kMatch, pid(9), std::nullopt).has_value());
    EXPECT_EQ(m_matches->listEvents(kMatch)->at(0).minute, 25);
}

TEST_F(MatchDayServiceTest, RecomputeKeepsRatingsAndRpe) {
    startMatch();
    advance();
    ASSERT_TRUE(sql("UPDATE appearance SET rating = 8, rpe = 7 WHERE player_id = 9"));
    m_clock.advance(10min);
    ASSERT_TRUE(m_service->recordGoal(kMatch, pid(9), std::nullopt).has_value());
    EXPECT_EQ(appearance(9).rating, 8);
    EXPECT_EQ(appearance(9).rpe, 7);
}

TEST_F(MatchDayServiceTest, StateOfAMatchWithoutMatchDayData) {
    logInAs(Role::Analyst);
    auto state = m_service->state(kFinished);
    ASSERT_TRUE(state.has_value()) << state.error().detail;
    EXPECT_FALSE(state->hasLineup);
    EXPECT_FALSE(state->clock.has_value());
    EXPECT_EQ(state->situation.goalsFor, 1);
    EXPECT_FALSE(state->lastAction.has_value());
    EXPECT_EQ(state->match.opponentName, "Rywal");
}

// --- P13 review ---

namespace {

// Every row Match Day writes, as text: equal before and after a failed action means nothing of it
// stayed.
QString snapshot(const DatabaseManager& db) {
    QStringList rows;
    for (const char* table : {"match", "match_event", "appearance", "match_live",
                              "match_clock_step", "match_tactic_change"}) {
        QSqlQuery query(db.database());
        EXPECT_TRUE(query.exec(
            QStringLiteral("SELECT * FROM %1 ORDER BY id").arg(QLatin1StringView(table))));
        while (query.next()) {
            QStringList values{QString::fromLatin1(table)};
            for (int column = 0; column < query.record().count(); ++column) {
                values << (query.value(column).isNull() ? QStringLiteral("NULL")
                                                        : query.value(column).toString());
            }
            rows << values.join(u'|');
        }
    }
    return rows.join(u'\n');
}

} // namespace

TEST_F(MatchDayServiceTest, UndoOfTheEndIsRefusedWhileAnotherMatchIsLive) {
    // Codex: finish match 1, start match 6, then undo the end of match 1.
    startMatch();
    advance();
    m_clock.advance(46min);
    advance();
    m_clock.advance(15min);
    advance();
    m_clock.advance(46min);
    advance();
    ASSERT_EQ(m_matches->get(kMatch)->status, MatchStatus::Finished);
    ASSERT_TRUE(m_lineups->saveForMatch(kOtherPlanned, lineup()));
    ASSERT_TRUE(m_service->start(kOtherPlanned, kHalves, 4).has_value());

    const QString before = snapshot(*m_db);
    expectError(m_service->undoLast(kMatch), ErrorCode::Conflict, "undo_end_another_live");
    EXPECT_EQ(snapshot(*m_db), before);
    EXPECT_EQ(m_matches->get(kMatch)->status, MatchStatus::Finished);

    // Once the other match is planned again, the end can be undone.
    ASSERT_TRUE(m_service->cancelStart(kOtherPlanned).has_value());
    const auto undone = m_service->undoLast(kMatch);
    ASSERT_TRUE(undone.has_value()) << undone.error().detail;
    EXPECT_EQ(m_matches->get(kMatch)->status, MatchStatus::Live);
}

TEST_F(MatchDayServiceTest, AnInconsistentHistoryIsAnErrorNotALineupAtNil) {
    startMatch();
    advance();
    // Player 1 starts, so he cannot come on.
    ASSERT_TRUE(
        sql("INSERT INTO match_event (match_id, minute, type, player_id, related_player_id) "
            "VALUES (1, 5, 'sub', 1, 2)"));
    expectError(m_service->state(kMatch), ErrorCode::Database, "match_history");
    m_clock.advance(10min);
    expectError(m_service->recordGoal(kMatch, pid(9), std::nullopt), ErrorCode::Database,
                "match_history");
    // A match Match Day never started still shows its lineup and the stored score.
    EXPECT_TRUE(m_service->state(kFinished).has_value());
}

TEST_F(MatchDayServiceTest, AYellowAndAStraightRedAreTwoActions) {
    startMatch();
    advance();
    m_clock.advance(10min);
    ASSERT_TRUE(m_service->recordYellow(kMatch, pid(4)).has_value());
    ASSERT_TRUE(m_service->recordRed(kMatch, pid(4)).has_value()); // the same minute
    EXPECT_EQ(read().lastAction->kind, MatchDayAction::Kind::Event);
    const auto undone = m_service->undoLast(kMatch);
    ASSERT_TRUE(undone.has_value());
    EXPECT_EQ(undone->kind, MatchDayAction::Kind::Event);
    EXPECT_EQ(undone->event->type, MatchEventType::Red);
    EXPECT_EQ(eventCount(), 1);
}

TEST_F(MatchDayServiceTest, ASecondYellowIsFoundByCountingNotByNeighbours) {
    startMatch();
    advance();
    m_clock.advance(5min);
    ASSERT_TRUE(m_service->recordYellow(kMatch, pid(4)).has_value());
    m_clock.advance(10min);
    ASSERT_TRUE(m_service->recordGoal(kMatch, pid(9), std::nullopt).has_value());
    m_clock.advance(10min);
    ASSERT_TRUE(m_service->recordYellow(kMatch, pid(4)).has_value()); // second: yellow + red
    const MatchDayState state = read();
    ASSERT_EQ(state.lastAction->kind, MatchDayAction::Kind::SecondYellow);
    EXPECT_EQ(state.lastAction->event->minute, 25);
    const auto undone = m_service->undoLast(kMatch);
    ASSERT_TRUE(undone.has_value());
    // The goal and the first yellow stay.
    EXPECT_EQ(eventCount(), 2);
    expectMinutes(4, 0, std::nullopt);
}

// Atomicity: a forced SQL failure at each point of an action leaves the database as before it.
TEST_F(MatchDayServiceTest, AFailureBeforeTheMinutesAreWrittenKeepsTheEventOut) {
    startMatch();
    advance();
    m_clock.advance(10min);
    const QString before = snapshot(*m_db);
    ASSERT_TRUE(sql("CREATE TEMP TRIGGER fail_minutes BEFORE INSERT ON appearance "
                    "BEGIN SELECT RAISE(ABORT, 'forced'); END"));
    EXPECT_FALSE(m_service->recordGoal(kMatch, pid(9), std::nullopt).has_value());
    EXPECT_EQ(snapshot(*m_db), before);
    ASSERT_TRUE(sql("DROP TRIGGER fail_minutes"));
    EXPECT_TRUE(m_service->recordGoal(kMatch, pid(9), std::nullopt).has_value());
}

TEST_F(MatchDayServiceTest, AFailureBetweenTheSecondYellowAndItsRedKeepsBothOut) {
    startMatch();
    advance();
    m_clock.advance(5min);
    ASSERT_TRUE(m_service->recordYellow(kMatch, pid(4)).has_value());
    m_clock.advance(10min);
    const QString before = snapshot(*m_db);
    ASSERT_TRUE(sql("CREATE TEMP TRIGGER fail_red BEFORE INSERT ON match_event "
                    "WHEN NEW.type = 'red' BEGIN SELECT RAISE(ABORT, 'forced'); END"));
    EXPECT_FALSE(m_service->recordYellow(kMatch, pid(4)).has_value());
    EXPECT_EQ(snapshot(*m_db), before);
    ASSERT_TRUE(sql("DROP TRIGGER fail_red"));
    EXPECT_TRUE(m_service->recordYellow(kMatch, pid(4)).has_value());
    EXPECT_EQ(eventCount(), 3);
}

TEST_F(MatchDayServiceTest, AFailureAtTheEndKeepsTheMatchLive) {
    startMatch();
    advance();
    m_clock.advance(46min);
    advance();
    m_clock.advance(15min);
    advance();
    m_clock.advance(46min);
    const QString before = snapshot(*m_db);
    ASSERT_TRUE(sql("CREATE TEMP TRIGGER fail_end BEFORE UPDATE ON match "
                    "WHEN NEW.status = 'finished' BEGIN SELECT RAISE(ABORT, 'forced'); END"));
    EXPECT_FALSE(m_service->advanceClock(kMatch).has_value());
    EXPECT_EQ(snapshot(*m_db), before);
    EXPECT_EQ(m_matches->get(kMatch)->status, MatchStatus::Live);
    ASSERT_TRUE(sql("DROP TRIGGER fail_end"));
    advance();
    EXPECT_EQ(m_matches->get(kMatch)->status, MatchStatus::Finished);
}

TEST_F(MatchDayServiceTest, AFailureWhileUndoingKeepsWhatWasThere) {
    startMatch();
    advance();
    m_clock.advance(5min);
    ASSERT_TRUE(m_service->recordYellow(kMatch, pid(4)).has_value());
    m_clock.advance(10min);
    ASSERT_TRUE(m_service->recordYellow(kMatch, pid(4)).has_value());
    // Both rows of the action go in one statement; the failure comes at the yellow one.
    QString before = snapshot(*m_db);
    ASSERT_TRUE(sql("CREATE TEMP TRIGGER fail_undo BEFORE DELETE ON match_event "
                    "WHEN OLD.type = 'yellow' BEGIN SELECT RAISE(ABORT, 'forced'); END"));
    EXPECT_FALSE(m_service->undoLast(kMatch).has_value());
    EXPECT_EQ(snapshot(*m_db), before);
    ASSERT_TRUE(sql("DROP TRIGGER fail_undo"));

    // Undo of the end: the failure comes when the match is made live again.
    m_clock.advance(31min);
    advance();
    m_clock.advance(15min);
    advance();
    m_clock.advance(46min);
    advance();
    before = snapshot(*m_db);
    ASSERT_TRUE(sql("CREATE TEMP TRIGGER fail_reopen BEFORE UPDATE ON match "
                    "WHEN NEW.status = 'live' BEGIN SELECT RAISE(ABORT, 'forced'); END"));
    EXPECT_FALSE(m_service->undoLast(kMatch).has_value());
    EXPECT_EQ(snapshot(*m_db), before);
    ASSERT_TRUE(sql("DROP TRIGGER fail_reopen"));
    EXPECT_TRUE(m_service->undoLast(kMatch).has_value());
}

namespace {

// The services of Match Day on one database, as the app builds them.
struct MatchDayStack {
    explicit MatchDayStack(DatabaseManager& db, FakeClock& clock)
        : staff(db), clubs(db), lineups(db), matches(db), live(db), notes(db), tactics(db),
          opponents(db), players(db), availability(db), training(db),
          auth(db, staff, session, clock, 4),
          lineupService(db, lineups, matches, opponents, players, availability, training, session,
                        clock),
          matchDay(db, matches, live, notes, tactics, lineups, clubs, opponents, players,
                   lineupService, session, clock) {}

    StaffRepository staff;
    ClubRepository clubs;
    LineupRepository lineups;
    MatchRepository matches;
    MatchLiveRepository live;
    MatchNoteRepository notes;
    MatchTacticRepository tactics;
    OpponentRepository opponents;
    PlayerRepository players;
    AvailabilityRepository availability;
    TrainingRepository training;
    Session session;
    AuthService auth;
    LineupService lineupService;
    MatchDayService matchDay;
};

class MatchDayRestartTest : public TempDatabaseTest {
protected:
    [[nodiscard]] std::unique_ptr<DatabaseManager> open() const {
        auto db = DatabaseManager::open(databasePath(), options());
        EXPECT_TRUE(db.has_value()) << db.error().detail;
        return db ? std::move(*db) : nullptr;
    }

    FakeClock m_clock;
};

} // namespace

// Close the app at 20:00 and open it 5 minutes later: a new DatabaseManager on the same file, new
// services; the clock shows 25:00 and the match goes on.
TEST_F(MatchDayRestartTest, AFileDatabaseResumesTheRunningMatch) {
    StaffUserId coach;
    {
        auto db = open();
        ASSERT_NE(db, nullptr);
        const QSqlDatabase raw = db->database();
        ASSERT_TRUE(run(raw, QStringLiteral("INSERT INTO club (id, name) VALUES (1, 'KS Test')")));
        ASSERT_TRUE(
            run(raw, QStringLiteral("INSERT INTO team (id, club_id, name, category, season) "
                                    "VALUES (1, 1, 'Seniorzy', 'senior', '2026/27')")));
        ASSERT_TRUE(run(raw, QStringLiteral("INSERT INTO opponent (id, name, name_key) "
                                            "VALUES (1, 'Rywal', 'rywal')")));
        ASSERT_TRUE(run(raw, QStringLiteral("INSERT INTO match (id, team_id, opponent_id, date, "
                                            "venue) VALUES (1, 1, 1, '2026-11-01', 'home')")));
        for (int id = 1; id <= 14; ++id) {
            ASSERT_TRUE(
                run(raw,
                    QStringLiteral("INSERT INTO player (id, team_id, first_name, last_name, "
                                   "shirt_number, primary_position) VALUES (?, 1, 'P', ?, "
                                   "?, ?)"),
                    {id, QStringLiteral("Player%1").arg(id), id,
                     id == 1 ? QStringLiteral("gk") : QStringLiteral("cm")}));
        }
        MatchDayStack stack(*db, m_clock);
        StaffUserDraft draft;
        draft.name = "Trener";
        draft.role = Role::Coach;
        const auto hash = stack.auth.hashPin("1234");
        ASSERT_TRUE(hash.has_value());
        coach = stack.staff.create(draft, *hash).value();
        ASSERT_TRUE(stack.auth.login(coach, "1234").has_value());
        Lineup lineup = emptyLineup(*findFormation("4-4-2"));
        for (std::size_t slot = 0; slot < lineup.pitchSlots.size(); ++slot) {
            lineup.pitchSlots[slot].player = pid(static_cast<std::int64_t>(slot) + 1);
        }
        lineup.bench = {pid(12), pid(13), pid(14)};
        ASSERT_TRUE(stack.lineups.saveForMatch(kMatchId, lineup));
        ASSERT_TRUE(stack.matchDay.start(kMatchId, kHalves, 4).has_value());
        ASSERT_TRUE(stack.matchDay.advanceClock(kMatchId).has_value());
        m_clock.advance(12min);
        ASSERT_TRUE(stack.matchDay.recordGoal(kMatchId, pid(9), std::nullopt).has_value());
        m_clock.advance(8min); // 20:00, the app closes
    }
    m_clock.advance(5min);

    auto db = open();
    ASSERT_NE(db, nullptr);
    MatchDayStack stack(*db, m_clock);
    ASSERT_TRUE(stack.auth.login(coach, "1234").has_value());
    EXPECT_EQ(stack.matchDay.liveMatch(TeamId{1}).value(), kMatchId);
    const auto state = stack.matchDay.state(kMatchId);
    ASSERT_TRUE(state.has_value()) << state.error().detail;
    ASSERT_TRUE(state->clock.has_value());
    const ClockReading reading = readClock(*state->clock, m_clock.now());
    EXPECT_EQ(reading.phase, MatchPhase::Running);
    EXPECT_EQ(reading.regular, 25min);
    EXPECT_EQ(state->situation.goalsFor, 1);
    ASSERT_EQ(state->events.size(), 1U);
    EXPECT_EQ(state->events.front().minute, 12);
    ASSERT_TRUE(stack.matchDay.recordGoal(kMatchId, pid(10), std::nullopt).has_value());
    EXPECT_EQ(stack.matches.listEvents(kMatchId)->back().minute, 25);
    EXPECT_EQ(stack.matches.listAppearances(kMatchId)->size(), 11U);
}

// --- Manual test of P13: late entry, match format, minimum of players, notes ---

TEST_F(MatchDayServiceTest, AnEventCanBeEnteredLateAtAPlayedMinute) {
    startMatch();
    advance();
    m_clock.advance(30min);
    // A goal at 12' (stored 11), entered at 30:00.
    ASSERT_TRUE(m_service->recordGoal(kMatch, pid(9), pid(10), MatchTime{11, std::nullopt}));
    // A substitution at 20' (stored 19): the minutes follow it.
    ASSERT_TRUE(
        m_service->recordSubstitution(kMatch, pid(12), pid(5), MatchTime{19, std::nullopt}));
    expectMinutes(5, 0, 19);
    expectMinutes(12, 19, std::nullopt);
    // Not yet played: 32' (stored 31) at 30:00, or stoppage time of the first half.
    expectError(m_service->recordGoal(kMatch, pid(9), std::nullopt, MatchTime{31, std::nullopt}),
                ErrorCode::InvalidInput, "minute_not_played");
    expectError(m_service->recordGoal(kMatch, pid(9), std::nullopt, MatchTime{45, 1}),
                ErrorCode::InvalidInput, "minute_not_played");
    // The events come in the order of the match.
    const auto events = m_matches->listEvents(kMatch);
    ASSERT_EQ(events->size(), 2U);
    EXPECT_EQ(events->at(0).minute, 11);
    // Undo takes back the substitution, the action entered last, not the latest minute.
    m_clock.advance(1min);
    ASSERT_TRUE(m_service->recordGoal(kMatch, pid(10), std::nullopt, MatchTime{2, std::nullopt}));
    const auto undone = m_service->undoLast(kMatch);
    ASSERT_TRUE(undone.has_value());
    EXPECT_EQ(undone->event->minute, 2);
    EXPECT_EQ(m_matches->listEvents(kMatch)->size(), 2U);
}

TEST_F(MatchDayServiceTest, ALateEventThatALaterOneNoLongerFitsIsRefusedNamingIt) {
    startMatch();
    advance();
    m_clock.advance(60min); // stoppage time of the first half: 45+16
    ASSERT_TRUE(m_service->recordGoal(kMatch, pid(9), std::nullopt, MatchTime{40, std::nullopt}));
    const MatchEvent goal = m_matches->listEvents(kMatch)->front();
    // A red card for the scorer at 31' would send him off before his goal.
    MatchEventDraft red;
    red.type = MatchEventType::Red;
    red.playerId = pid(9);
    red.minute = 30;
    const auto check = m_service->checkEvent(kMatch, red);
    ASSERT_TRUE(check.has_value()) << check.error().detail;
    ASSERT_TRUE(check->has_value());
    EXPECT_EQ((*check)->problem, MatchEventProblem::SentOff);
    ASSERT_TRUE((*check)->laterEvent.has_value());
    EXPECT_EQ((*check)->laterEvent->id, goal.id);
    const int events = eventCount();
    expectError(m_service->recordRed(kMatch, pid(9), MatchTime{30, std::nullopt}),
                ErrorCode::Conflict, "event_conflict");
    EXPECT_EQ(eventCount(), events);
    // The new event itself not fitting is named by its own problem.
    MatchEventDraft sub;
    sub.type = MatchEventType::Sub;
    sub.playerId = pid(9);
    sub.relatedPlayerId = pid(10);
    sub.minute = 30;
    const auto own = m_service->checkEvent(kMatch, sub);
    ASSERT_TRUE(own.has_value() && own->has_value());
    EXPECT_FALSE((*own)->laterEvent.has_value());
    EXPECT_EQ((*own)->problem, MatchEventProblem::NotOnBench);
    // A fitting event: no conflict.
    red.playerId = pid(4);
    EXPECT_FALSE(m_service->checkEvent(kMatch, red).value().has_value());
}

TEST_F(MatchDayServiceTest, ALateYellowCountsTheCardsAtItsTime) {
    startMatch();
    advance();
    m_clock.advance(40min);
    ASSERT_TRUE(m_service->recordYellow(kMatch, pid(4), MatchTime{30, std::nullopt}));
    // A yellow at 21' entered later is the first; the one at 31' becomes the second without its
    // red: refused, naming the card at 31'.
    MatchEventDraft yellow;
    yellow.type = MatchEventType::Yellow;
    yellow.playerId = pid(4);
    yellow.minute = 20;
    const auto check = m_service->checkEvent(kMatch, yellow);
    ASSERT_TRUE(check.has_value() && check->has_value());
    EXPECT_EQ((*check)->problem, MatchEventProblem::SecondYellowWithoutRed);
    EXPECT_EQ((*check)->laterEvent->minute, 30);
    // A yellow at 36' entered late is the second: it brings its red at that time.
    ASSERT_TRUE(m_service->recordYellow(kMatch, pid(4), MatchTime{35, std::nullopt}));
    expectMinutes(4, 0, 35);
    EXPECT_EQ(read().lastAction->kind, MatchDayAction::Kind::SecondYellow);
}

TEST_F(MatchDayServiceTest, ThreePartsOfThirty) {
    logInAs(Role::Coach);
    saveLineup();
    ASSERT_TRUE(m_service->start(kMatch, {3, 30}, 4).has_value());
    advance(); // 1st part
    m_clock.advance(31min + 10s);
    ASSERT_TRUE(m_service->recordGoal(kMatch, pid(9), std::nullopt)); // 30+2'
    advance();                                                        // end of the 1st part
    m_clock.advance(10min);
    ASSERT_TRUE(m_service->recordSubstitution(kMatch, pid(12), pid(5))); // in the break: 31'
    advance();                                                           // 2nd part
    m_clock.advance(30min + 30s);
    ASSERT_TRUE(m_service->recordOpponentGoal(kMatch)); // 60+1'
    advance();
    m_clock.advance(5min);
    advance(); // 3rd part
    m_clock.advance(31min);
    advance(); // the end
    const auto events = m_matches->listEvents(kMatch);
    ASSERT_EQ(events->size(), 3U);
    EXPECT_EQ((MatchTime{events->at(0).minute, events->at(0).addedTime}), (MatchTime{30, 2}));
    EXPECT_EQ((MatchTime{events->at(1).minute, events->at(1).addedTime}),
              (MatchTime{30, std::nullopt}));
    EXPECT_EQ((MatchTime{events->at(2).minute, events->at(2).addedTime}), (MatchTime{60, 1}));
    EXPECT_EQ(m_matches->get(kMatch)->status, MatchStatus::Finished);
    expectMinutes(5, 0, 30);
    expectMinutes(12, 30, 90);
    expectMinutes(9, 0, 90);
}

TEST_F(MatchDayServiceTest, FewerThanSevenPlayersEndTheMatch) {
    startMatch();
    advance();
    m_clock.advance(10min);
    for (const std::int64_t player : {1, 2, 4, 5}) {
        ASSERT_TRUE(m_service->recordRed(kMatch, pid(player)));
    }
    EXPECT_EQ(onPitchCount(read().situation), 7);
    m_clock.advance(10min);
    ASSERT_TRUE(m_service->recordRed(kMatch, pid(6))); // 6 left
    EXPECT_EQ(onPitchCount(read().situation), 6);
    // Only the end, notes and undo.
    expectError(m_service->recordGoal(kMatch, pid(9), std::nullopt), ErrorCode::Conflict,
                "too_few_players");
    expectError(m_service->recordSubstitution(kMatch, pid(12), pid(9)), ErrorCode::Conflict,
                "too_few_players");
    expectError(m_service->advanceClock(kMatch), ErrorCode::Conflict, "too_few_players");
    EXPECT_TRUE(m_service->addNote(kMatch, std::nullopt, std::nullopt, "Mecz przerwany"));
    ASSERT_TRUE(m_service->endMatch(kMatch).has_value());
    const auto match = m_matches->get(kMatch);
    EXPECT_EQ(match->status, MatchStatus::Finished);
    EXPECT_EQ(match->goalsFor, 0);
    // Ended at 20:00: those on the pitch play until 20, the final minute.
    expectMinutes(9, 0, 20);
    expectMinutes(6, 0, 20);
    expectMinutes(5, 0, 10);
    EXPECT_EQ(finalMinute(*read().clock), 20);
    // Undo: live again, still 6 players; then the red goes and the match goes on.
    const auto finishUndone = m_service->undoLast(kMatch);
    ASSERT_TRUE(finishUndone.has_value());
    EXPECT_EQ(finishUndone->kind, MatchDayAction::Kind::Finish);
    EXPECT_EQ(m_matches->get(kMatch)->status, MatchStatus::Live);
    ASSERT_TRUE(m_service->undoLast(kMatch)); // the note
    ASSERT_TRUE(m_service->undoLast(kMatch)); // the red of 6
    EXPECT_TRUE(m_service->recordGoal(kMatch, pid(9), std::nullopt).has_value());
}

TEST_F(MatchDayServiceTest, EndingEarlyInABreakEndsAtTheBreak) {
    // Codex: the first half ends at 44:30, a substitution in the break (at 45), red cards entered
    // late down to 6 players, then the end. The final minute is the break (45), so the substitute
    // has 45–45 and the end goes through.
    startMatch();
    advance();
    m_clock.advance(44min + 30s);
    advance(); // half time at 44:30
    m_clock.advance(5min);
    ASSERT_TRUE(m_service->recordSubstitution(kMatch, pid(12), pid(5)));
    // With 11 players the match ends only with the clock.
    expectError(m_service->endMatch(kMatch), ErrorCode::Conflict, "match_not_over");
    for (const std::int64_t player : {1, 2, 4, 6, 7}) {
        ASSERT_TRUE(m_service->recordRed(kMatch, pid(player), MatchTime{40, std::nullopt}));
    }
    ASSERT_EQ(onPitchCount(read().situation), 6);
    const auto ended = m_service->endMatch(kMatch);
    ASSERT_TRUE(ended.has_value()) << ended.error().detail;
    EXPECT_EQ(read().clock->endedEarlyAt, 45);
    EXPECT_EQ(finalMinute(*read().clock), 45);
    expectMinutes(12, 45, 45);
    expectMinutes(9, 0, 45);
    expectMinutes(1, 0, 40);
    expectError(m_service->endMatch(kMatch), ErrorCode::Conflict, "match_not_live");
}

TEST_F(MatchDayServiceTest, TheClockButtonEndsEarlyWithFewerThanSeven) {
    // 2×45, five red cards by 60', then the ordinary clock button "End match" at 60:00.
    startMatch();
    advance();
    m_clock.advance(46min);
    advance();
    m_clock.advance(15min);
    advance();              // second half
    m_clock.advance(15min); // 60:00
    for (const std::int64_t player : {1, 2, 4, 6, 7}) {
        ASSERT_TRUE(m_service->recordRed(kMatch, pid(player)));
    }
    advance(); // the end of the last part, with 6 players: an early end
    EXPECT_EQ(m_matches->get(kMatch)->status, MatchStatus::Finished);
    EXPECT_EQ(read().clock->endedEarlyAt, 60);
    expectMinutes(9, 0, 60);
    expectMinutes(3, 0, 60);
}

TEST_F(MatchDayServiceTest, WithSevenOrMoreTheEndIsNormal) {
    startMatch();
    advance();
    m_clock.advance(46min);
    advance();
    m_clock.advance(15min);
    advance();
    m_clock.advance(20min); // 65:00, the last part runs: endMatch is the clock button
    ASSERT_TRUE(m_service->endMatch(kMatch).has_value());
    EXPECT_EQ(read().clock->endedEarlyAt, std::nullopt);
    expectMinutes(9, 0, 90);
}

TEST_F(MatchDayServiceTest, ASeqIsNeverUsedTwice) {
    // Codex: a note, deleted; an opponent goal; the note restored; undo takes back the goal only.
    startMatch();
    advance();
    m_clock.advance(10min);
    const auto note = m_service->addNote(kMatch, std::nullopt, std::nullopt, "Wiatr");
    ASSERT_TRUE(note.has_value());
    const auto deleted = m_service->deleteNote(kMatch, *note);
    ASSERT_TRUE(deleted.has_value());
    ASSERT_TRUE(m_service->recordOpponentGoal(kMatch));
    const MatchEvent goal = m_matches->listEvents(kMatch)->front();
    ASSERT_TRUE(goal.seq.has_value());
    EXPECT_GT(*goal.seq, deleted->seq.value());
    ASSERT_TRUE(m_service->restoreNote(*deleted).has_value());
    EXPECT_EQ(read().notes.front().seq, deleted->seq);
    EXPECT_EQ(read().lastAction->kind, MatchDayAction::Kind::Event);

    const auto undone = m_service->undoLast(kMatch);
    ASSERT_TRUE(undone.has_value());
    EXPECT_EQ(undone->kind, MatchDayAction::Kind::Event);
    EXPECT_EQ(undone->event->type, MatchEventType::OpponentGoal);
    EXPECT_EQ(eventCount(), 0);
    ASSERT_EQ(read().notes.size(), 1U); // the note stays
    // Next: the note, then the kick-off.
    EXPECT_EQ(m_service->undoLast(kMatch)->kind, MatchDayAction::Kind::Note);
    EXPECT_EQ(m_service->undoLast(kMatch)->kind, MatchDayAction::Kind::ClockStep);
}

TEST_F(MatchDayServiceTest, NotesWithTheirMinute) {
    startMatch();
    expectError(m_service->addNote(kMatch, std::nullopt, std::nullopt, "Przed meczem"),
                ErrorCode::Conflict, "match_not_running");
    advance();
    m_clock.advance(20min);
    const auto appearancesBefore = count("SELECT count(*) FROM appearance");
    const auto first = m_service->addNote(kMatch, std::nullopt, pid(9), "  Uraz kostki  ");
    ASSERT_TRUE(first.has_value()) << first.error().detail;
    const auto late = m_service->addNote(kMatch, MatchTime{4, std::nullopt}, std::nullopt, "Wiatr");
    ASSERT_TRUE(late.has_value());
    expectError(m_service->addNote(kMatch, MatchTime{30, std::nullopt}, std::nullopt, "x"),
                ErrorCode::InvalidInput, "minute_not_played");
    expectError(m_service->addNote(kMatch, std::nullopt, std::nullopt, "   "),
                ErrorCode::InvalidInput, "note_text");
    expectError(m_service->addNote(kMatch, std::nullopt, std::nullopt, std::string(501, 'x')),
                ErrorCode::InvalidInput, "note_text");
    expectError(m_service->addNote(kMatch, std::nullopt, pid(99), "x"), ErrorCode::InvalidInput,
                "player_unknown");

    MatchDayState state = read();
    ASSERT_EQ(state.notes.size(), 2U);
    EXPECT_EQ(state.notes.front().text, "Wiatr"); // 5' before 21'
    EXPECT_EQ(state.notes.back().text, "Uraz kostki");
    EXPECT_EQ(state.notes.back().minute, 20);
    EXPECT_EQ(state.notes.back().playerId, pid(9));
    EXPECT_EQ(state.situation.goalsFor, 0);
    EXPECT_EQ(count("SELECT count(*) FROM appearance"), appearancesBefore);
    EXPECT_EQ(state.lastAction->kind, MatchDayAction::Kind::Note);

    ASSERT_TRUE(
        m_service->updateNote(kMatch, *first, MatchTime{19, std::nullopt}, std::nullopt, "Kostka"));
    state = read();
    EXPECT_EQ(state.notes.back().text, "Kostka");
    EXPECT_EQ(state.notes.back().playerId, std::nullopt);
    const auto deleted = m_service->deleteNote(kMatch, *first);
    ASSERT_TRUE(deleted.has_value());
    EXPECT_EQ(read().notes.size(), 1U);
    ASSERT_TRUE(m_service->restoreNote(*deleted).has_value());
    EXPECT_EQ(read().notes.size(), 2U);
    // Undo takes back the note added last.
    const auto undone = m_service->undoLast(kMatch);
    ASSERT_TRUE(undone.has_value());
    EXPECT_EQ(undone->kind, MatchDayAction::Kind::Note);
    EXPECT_EQ(undone->note->text, "Wiatr");
    ASSERT_EQ(read().notes.size(), 1U);
    // Another role reads them; only RunMatchDay writes.
    logInAs(Role::Physio);
    EXPECT_EQ(read().notes.size(), 1U);
    expectError(m_service->addNote(kMatch, std::nullopt, std::nullopt, "x"),
                ErrorCode::PermissionDenied);
}

// --- Set clock and the mode without the clock (P13 manual test) ---

TEST_F(MatchDayServiceTest, SetClockMovesTheStartOfTheRunningPart) {
    startMatch();
    advance(); // the kick-off pressed late: the match had run 3 minutes already
    auto previous = m_service->setClock(kMatch, 3min);
    ASSERT_TRUE(previous.has_value()) << previous.error().detail;
    EXPECT_EQ(readClock(*read().clock, m_clock.now()).regular, 3min);
    m_clock.advance(7min);
    EXPECT_EQ(readClock(*read().clock, m_clock.now()).regular, 10min); // it runs on
    ASSERT_TRUE(m_service->recordGoal(kMatch, pid(9), std::nullopt));  // at 11' (stored 10)
    // Not before the goal of this part, not past 45 + 30 minutes.
    expectError(m_service->setClock(kMatch, 9min), ErrorCode::InvalidInput, "clock_before_event");
    expectError(m_service->setClock(kMatch, 75min + 1s), ErrorCode::InvalidInput, "clock_time");
    previous = m_service->setClock(kMatch, 20min);
    ASSERT_TRUE(previous.has_value());
    EXPECT_EQ(readClock(*read().clock, m_clock.now()).regular, 20min);
    // The undo toast brings the start back; the goal and the seq stay.
    ASSERT_TRUE(m_service->restoreClock(kMatch, *previous).has_value());
    EXPECT_EQ(readClock(*read().clock, m_clock.now()).regular, 10min);
    EXPECT_EQ(read().lastAction->kind, MatchDayAction::Kind::Event);
    // In the break nothing runs.
    advance();
    expectError(m_service->setClock(kMatch, 10min), ErrorCode::Conflict, "clock_not_running");
    logInAs(Role::Physio);
    expectError(m_service->setClock(kMatch, 10min), ErrorCode::PermissionDenied);
}

TEST_F(MatchDayServiceTest, SetClockInTheSecondHalfKeepsItsEvents) {
    startMatch();
    advance();
    m_clock.advance(46min);
    advance();
    m_clock.advance(15min);
    advance(); // second half
    m_clock.advance(10min);
    ASSERT_TRUE(m_service->recordGoal(kMatch, pid(9), std::nullopt)); // 56' (stored 55)
    expectError(m_service->setClock(kMatch, 9min), ErrorCode::InvalidInput, "clock_before_event");
    ASSERT_TRUE(m_service->setClock(kMatch, 12min + 30s).has_value());
    EXPECT_EQ(readClock(*read().clock, m_clock.now()).regular, 57min + 30s);
    // After a restart the moved clock is there.
    restart();
    m_clock.advance(1min);
    EXPECT_EQ(readClock(*read().clock, m_clock.now()).regular, 58min + 30s);
}

TEST_F(MatchDayServiceTest, AWholeMatchWithoutTheClockInAnyOrder) {
    logInAs(Role::Coach);
    saveLineup();
    ASSERT_TRUE(m_service->start(kMatch, kHalves, 4, true).has_value());
    MatchDayState state = read();
    ASSERT_TRUE(state.clock->clockless);
    EXPECT_EQ(phaseOf(*state.clock), MatchPhase::Running);
    expectError(m_service->advanceClock(kMatch), ErrorCode::Conflict, "clockless");
    expectError(m_service->setClock(kMatch, 5min), ErrorCode::Conflict, "clock_not_running");
    expectError(m_service->recordGoal(kMatch, pid(9), std::nullopt), ErrorCode::InvalidInput,
                "minute_required");
    expectError(m_service->recordGoal(kMatch, pid(9), std::nullopt, MatchTime{90, std::nullopt}),
                ErrorCode::InvalidInput, "minute_out_of_match");
    // Entered after the match, out of order: no "not played yet".
    ASSERT_TRUE(
        m_service->recordSubstitution(kMatch, pid(12), pid(5), MatchTime{59, std::nullopt}));
    ASSERT_TRUE(m_service->recordGoal(kMatch, pid(12), pid(9), MatchTime{74, std::nullopt}));
    ASSERT_TRUE(m_service->recordGoal(kMatch, pid(9), std::nullopt, MatchTime{9, std::nullopt}));
    ASSERT_TRUE(m_service->recordYellow(kMatch, pid(4), MatchTime{49, std::nullopt}));
    ASSERT_TRUE(m_service->recordOpponentGoal(kMatch, MatchTime{45, 2})); // 45+2'
    // A yellow at 30' would make the one at 50' the second without its red: refused.
    expectError(m_service->recordYellow(kMatch, pid(4), MatchTime{29, std::nullopt}),
                ErrorCode::Conflict, "event_conflict");
    state = read();
    EXPECT_EQ(state.events.size(), 5U);
    ASSERT_TRUE(m_service->recordYellow(kMatch, pid(4), MatchTime{79, std::nullopt})); // second
    ASSERT_TRUE(m_service->addNote(kMatch, MatchTime{19, std::nullopt}, std::nullopt, "Deszcz"));
    // Still possible to cancel? No: there are events.
    expectError(m_service->cancelStart(kMatch), ErrorCode::Conflict, "match_started");
    // Ten players: an ordinary end, whole match.
    ASSERT_TRUE(m_service->endMatch(kMatch).has_value());
    const auto match = m_matches->get(kMatch);
    EXPECT_EQ(match->status, MatchStatus::Finished);
    EXPECT_EQ(match->goalsFor, 2);
    EXPECT_EQ(match->goalsAgainst, 1);
    EXPECT_EQ(read().clock->endedEarlyAt, std::nullopt);
    expectMinutes(5, 0, 59);
    expectMinutes(12, 59, 90);
    expectMinutes(4, 0, 79);
    expectMinutes(9, 0, 90);
    // Undo, resume after a restart, the same.
    ASSERT_TRUE(m_service->undoLast(kMatch));
    restart();
    EXPECT_EQ(m_matches->get(kMatch)->status, MatchStatus::Live);
    EXPECT_TRUE(read().clock->clockless);
}

TEST_F(MatchDayServiceTest, WithoutTheClockFewerThanSevenEndAtTheLastEventOrTheMinuteTyped) {
    logInAs(Role::Coach);
    saveLineup();
    ASSERT_TRUE(m_service->start(kMatch, kHalves, 4, true).has_value());
    for (const std::int64_t player : {1, 2, 4, 6, 7}) {
        ASSERT_TRUE(m_service->recordRed(kMatch, pid(player),
                                         MatchTime{30 + static_cast<int>(player), std::nullopt}));
    }
    // The last event is at 37; a minute typed before it does not count, after it does.
    expectError(m_service->endMatch(kMatch, 91), ErrorCode::InvalidInput, "minute_out_of_match");
    const auto ended = m_service->endMatch(kMatch, 20);
    ASSERT_TRUE(ended.has_value()) << ended.error().detail;
    EXPECT_EQ(read().clock->endedEarlyAt, 37);
    expectMinutes(9, 0, 37);
    ASSERT_TRUE(m_service->undoLast(kMatch));
    ASSERT_TRUE(m_service->endMatch(kMatch, 52).has_value());
    EXPECT_EQ(read().clock->endedEarlyAt, 52);
    expectMinutes(9, 0, 52);
}

TEST_F(MatchDayServiceTest, WithoutTheClockTheStartCanBeTakenBack) {
    logInAs(Role::Coach);
    saveLineup();
    ASSERT_TRUE(m_service->start(kMatch, kHalves, 4, true).has_value());
    ASSERT_TRUE(m_service->cancelStart(kMatch).has_value());
    EXPECT_EQ(m_matches->get(kMatch)->status, MatchStatus::Planned);
}

// --- Live tactics (packet P13b). The lineup of the fixture by slot: 0 GK 3, 1 RB 1, 2 CB 2, 3 CB
// 4, 4 LB 5, 5 RM 6, 6 CM 7, 7 CM 8, 8 LM 9, 9 ST 10, 10 ST 11. ---

namespace {

const LineupSlot& slotOfPlayer(const Lineup& shape, std::int64_t player) {
    for (const LineupSlot& slot : shape.pitchSlots) {
        if (slot.player == pid(player)) {
            return slot;
        }
    }
    ADD_FAILURE() << "player " << player << " is not on the pitch";
    return shape.pitchSlots.front();
}

} // namespace

TEST_F(MatchDayServiceTest, ATacticChangeIsStoredAndShapesTheBoard) {
    startMatch();
    advance();
    m_clock.advance(30min);
    const MatchDayState before = read();
    EXPECT_EQ(before.shape.formation, "4-4-2");
    const Lineup shape = changeShape(before.shape, *findFormation("4-2-3-1"));
    const auto id = m_service->recordTacticChange(kMatch, shape);
    ASSERT_TRUE(id.has_value()) << id.error().detail;

    const MatchDayState state = read();
    ASSERT_EQ(state.tacticChanges.size(), 1U);
    EXPECT_EQ(state.tacticChanges.front().minute, 30);
    EXPECT_EQ(state.tacticChanges.front().shape.formation, "4-2-3-1");
    EXPECT_EQ(state.shape.formation, "4-2-3-1");
    EXPECT_EQ(slotOfPlayer(state.shape, 7).position, Position::Dm);
    ASSERT_TRUE(state.lastAction.has_value());
    EXPECT_EQ(state.lastAction->kind, MatchDayAction::Kind::TacticChange);
    EXPECT_EQ(state.lastAction->tacticChange->id, *id);
    // The lineup and match.formation stay the starting ones; no players are stored.
    EXPECT_EQ(state.lineup.formation, "4-4-2");
    EXPECT_EQ(m_matches->get(kMatch)->formation, "4-4-2");
    EXPECT_EQ(count("SELECT count(*) FROM match_tactic_change WHERE tactic_json LIKE "
                    "'%\"playerId\":3%'"),
              0);
}

TEST_F(MatchDayServiceTest, UndoTakesBackATacticChangeInTheSameStackAsEvents) {
    startMatch();
    advance();
    m_clock.advance(10min);
    ASSERT_TRUE(m_service->recordGoal(kMatch, pid(10), std::nullopt).has_value());
    m_clock.advance(10min);
    ASSERT_TRUE(
        m_service->recordTacticChange(kMatch, changeShape(read().shape, *findFormation("4-3-3")))
            .has_value());
    auto undone = m_service->undoLast(kMatch);
    ASSERT_TRUE(undone.has_value()) << undone.error().detail;
    EXPECT_EQ(undone->kind, MatchDayAction::Kind::TacticChange);
    ASSERT_TRUE(undone->tacticChange.has_value());
    EXPECT_EQ(undone->tacticChange->minute, 20);
    EXPECT_EQ(count("SELECT count(*) FROM match_tactic_change"), 0);
    EXPECT_EQ(read().shape.formation, "4-4-2");
    // Then the goal.
    undone = m_service->undoLast(kMatch);
    ASSERT_TRUE(undone.has_value());
    EXPECT_EQ(undone->kind, MatchDayAction::Kind::Event);
    EXPECT_EQ(eventCount(), 0);
}

TEST_F(MatchDayServiceTest, ASubstituteTakesThePositionOfHisSlotAfterAChange) {
    startMatch();
    advance();
    m_clock.advance(30min);
    ASSERT_TRUE(
        m_service->recordTacticChange(kMatch, changeShape(read().shape, *findFormation("4-2-3-1")))
            .has_value());
    m_clock.advance(5min);
    // Player 11 (the right striker of 4-4-2) is the AM of 4-2-3-1.
    ASSERT_TRUE(m_service->recordSubstitution(kMatch, pid(12), pid(11)).has_value());
    EXPECT_EQ(appearance(12).position, Position::Am);
    EXPECT_EQ(slotOfPlayer(read().shape, 12).position, Position::Am);
    // A substitution entered late before the change: the position of the lineup, and the change
    // still applies to him afterwards.
    ASSERT_TRUE(
        m_service->recordSubstitution(kMatch, pid(13), pid(7), MatchTime{19, std::nullopt}));
    EXPECT_EQ(appearance(13).position, Position::Cm);
    EXPECT_EQ(slotOfPlayer(read().shape, 13).position, Position::Dm);
}

TEST_F(MatchDayServiceTest, UndoOfAChangeRecomputesThePositionsOfLaterSubstitutes) {
    startMatch();
    advance();
    m_clock.advance(40min);
    // A substitution at 36', then a change entered late at 31': no stored change is later, so it is
    // accepted, and the substitute plays the position his slot has after it.
    ASSERT_TRUE(
        m_service->recordSubstitution(kMatch, pid(12), pid(11), MatchTime{35, std::nullopt}));
    EXPECT_EQ(appearance(12).position, Position::St);
    ASSERT_TRUE(m_service->recordTacticChange(
        kMatch, changeShape(read().shape, *findFormation("4-2-3-1")), MatchTime{30, std::nullopt}));
    EXPECT_EQ(appearance(12).position, Position::Am);
    ASSERT_TRUE(m_service->undoLast(kMatch).has_value());
    EXPECT_EQ(appearance(12).position, Position::St);
}

TEST_F(MatchDayServiceTest, TacticChangesAreRefusedWhenTheyCannotApply) {
    startMatch();
    const Lineup lineup433 = changeShape(read().shape, *findFormation("4-3-3"));
    // Before the kick-off.
    expectError(m_service->recordTacticChange(kMatch, lineup433), ErrorCode::Conflict,
                "match_not_running");
    advance();
    m_clock.advance(30min);
    // Nothing changes.
    expectError(m_service->recordTacticChange(kMatch, read().shape), ErrorCode::InvalidInput,
                "tactic_unchanged");
    // Not a shape: 10 slots, a coordinate outside the pitch, two goalkeepers.
    Lineup broken = lineup433;
    broken.pitchSlots.pop_back();
    expectError(m_service->recordTacticChange(kMatch, broken), ErrorCode::InvalidInput,
                "tactic_shape");
    broken = lineup433;
    broken.pitchSlots[3].x = 1.5;
    expectError(m_service->recordTacticChange(kMatch, broken), ErrorCode::InvalidInput,
                "tactic_shape");
    broken = lineup433;
    broken.pitchSlots[3].position = Position::Gk;
    expectError(m_service->recordTacticChange(kMatch, broken), ErrorCode::InvalidInput,
                "tactic_shape");
    // Not played yet.
    expectError(m_service->recordTacticChange(kMatch, lineup433, MatchTime{40, std::nullopt}),
                ErrorCode::InvalidInput, "minute_not_played");
    // A change is never entered before a stored one.
    ASSERT_TRUE(m_service->recordTacticChange(kMatch, lineup433).has_value());
    expectError(m_service->recordTacticChange(kMatch,
                                              changeShape(read().shape, *findFormation("4-4-2")),
                                              MatchTime{20, std::nullopt}),
                ErrorCode::Conflict, "tactic_change_later");
    // The same minute is fine.
    ASSERT_TRUE(m_service
                    ->recordTacticChange(kMatch, changeShape(read().shape, *findFormation("4-4-2")),
                                         MatchTime{30, std::nullopt})
                    .has_value());
    EXPECT_EQ(read().shape.formation, "4-4-2");
}

TEST_F(MatchDayServiceTest, TacticChangesNeedSevenPlayersAndALiveMatch) {
    startMatch();
    advance();
    m_clock.advance(10min);
    // Seven players may still change the shape; six may not.
    for (const std::int64_t player : {1, 2, 4, 5}) {
        ASSERT_TRUE(m_service->recordRed(kMatch, pid(player)).has_value());
    }
    ASSERT_EQ(read().shape.formation, "4-2");
    ASSERT_TRUE(m_service->recordRed(kMatch, pid(6)).has_value());
    ASSERT_EQ(read().shape.formation, "3-2");
    expectError(
        m_service->recordTacticChange(kMatch, changeShape(read().shape, *findFormation("4-3-3"))),
        ErrorCode::Conflict, "too_few_players");
    expectError(m_service->recordTacticChange(kFinished, lineup()), ErrorCode::Conflict,
                "match_not_live");
}

TEST_F(MatchDayServiceTest, TacticChangesNeedRunMatchDay) {
    startMatch();
    advance();
    m_clock.advance(10min);
    const Lineup shape = changeShape(read().shape, *findFormation("4-3-3"));
    for (const Role role : {Role::Physio, Role::Analyst}) {
        SCOPED_TRACE(toDbText(role));
        logInAs(role);
        EXPECT_TRUE(m_service->state(kMatch).has_value());
        expectError(m_service->recordTacticChange(kMatch, shape), ErrorCode::PermissionDenied);
    }
    logInAs(Role::Assistant);
    EXPECT_TRUE(m_service->recordTacticChange(kMatch, shape).has_value());
}

TEST_F(MatchDayServiceTest, AFailedTacticChangeLeavesNothingBehind) {
    startMatch();
    advance();
    m_clock.advance(10min);
    ASSERT_TRUE(m_service->recordSubstitution(kMatch, pid(12), pid(11)).has_value());
    const Lineup shape = changeShape(read().shape, *findFormation("4-2-3-1"));
    QString before = snapshot(*m_db);
    // After the change is written, while the minutes are.
    ASSERT_TRUE(sql("CREATE TEMP TRIGGER fail_minutes BEFORE INSERT ON appearance "
                    "BEGIN SELECT RAISE(ABORT, 'forced'); END"));
    EXPECT_FALSE(m_service->recordTacticChange(kMatch, shape).has_value());
    EXPECT_EQ(snapshot(*m_db), before);
    ASSERT_TRUE(sql("DROP TRIGGER fail_minutes"));
    ASSERT_TRUE(m_service->recordTacticChange(kMatch, shape).has_value());
    // The undo: the change goes, then the minutes fail.
    before = snapshot(*m_db);
    ASSERT_TRUE(sql("CREATE TEMP TRIGGER fail_undo BEFORE INSERT ON appearance "
                    "BEGIN SELECT RAISE(ABORT, 'forced'); END"));
    EXPECT_FALSE(m_service->undoLast(kMatch).has_value());
    EXPECT_EQ(snapshot(*m_db), before);
    ASSERT_TRUE(sql("DROP TRIGGER fail_undo"));
    EXPECT_TRUE(m_service->undoLast(kMatch).has_value());
}

TEST_F(MatchDayServiceTest, BackToPlannedIsRefusedOnceAShapeWasChanged) {
    logInAs(Role::Coach);
    saveLineup();
    ASSERT_TRUE(m_service->start(kMatch, kHalves, 4, true).has_value());
    ASSERT_TRUE(m_service
                    ->recordTacticChange(kMatch, changeShape(read().shape, *findFormation("4-3-3")),
                                         MatchTime{10, std::nullopt})
                    .has_value());
    expectError(m_service->cancelStart(kMatch), ErrorCode::Conflict, "match_started");
    // Without the clock a minute is required.
    expectError(
        m_service->recordTacticChange(kMatch, changeShape(read().shape, *findFormation("4-4-2"))),
        ErrorCode::InvalidInput, "minute_required");
}

// Done when of P13b: 4-4-2 → 4-2-3-1 at 60', a substitute replaces a striker, the board shows the
// new shape; reopening the app shows the same.
TEST_F(MatchDayRestartTest, TheShapeSurvivesARestart) {
    StaffUserId coach;
    {
        auto db = open();
        ASSERT_NE(db, nullptr);
        const QSqlDatabase raw = db->database();
        ASSERT_TRUE(run(raw, QStringLiteral("INSERT INTO club (id, name) VALUES (1, 'KS Test')")));
        ASSERT_TRUE(
            run(raw, QStringLiteral("INSERT INTO team (id, club_id, name, category, season) "
                                    "VALUES (1, 1, 'Seniorzy', 'senior', '2026/27')")));
        ASSERT_TRUE(run(raw, QStringLiteral("INSERT INTO opponent (id, name, name_key) "
                                            "VALUES (1, 'Rywal', 'rywal')")));
        ASSERT_TRUE(run(raw, QStringLiteral("INSERT INTO match (id, team_id, opponent_id, date, "
                                            "venue) VALUES (1, 1, 1, '2026-11-01', 'home')")));
        for (int id = 1; id <= 14; ++id) {
            ASSERT_TRUE(
                run(raw,
                    QStringLiteral("INSERT INTO player (id, team_id, first_name, last_name, "
                                   "shirt_number, primary_position) VALUES (?, 1, 'P', ?, "
                                   "?, ?)"),
                    {id, QStringLiteral("Player%1").arg(id), id,
                     id == 1 ? QStringLiteral("gk") : QStringLiteral("cm")}));
        }
        MatchDayStack stack(*db, m_clock);
        StaffUserDraft draft;
        draft.name = "Trener";
        draft.role = Role::Coach;
        const auto hash = stack.auth.hashPin("1234");
        ASSERT_TRUE(hash.has_value());
        coach = stack.staff.create(draft, *hash).value();
        ASSERT_TRUE(stack.auth.login(coach, "1234").has_value());
        Lineup lineup = emptyLineup(*findFormation("4-4-2"));
        for (std::size_t slot = 0; slot < lineup.pitchSlots.size(); ++slot) {
            lineup.pitchSlots[slot].player = pid(static_cast<std::int64_t>(slot) + 1);
        }
        lineup.bench = {pid(12), pid(13), pid(14)};
        ASSERT_TRUE(stack.lineups.saveForMatch(kMatchId, lineup));
        ASSERT_TRUE(stack.matchDay.start(kMatchId, kHalves, 4, true).has_value());
        const auto shape = stack.matchDay.state(kMatchId)->shape;
        ASSERT_TRUE(stack.matchDay
                        .recordTacticChange(kMatchId, changeShape(shape, *findFormation("4-2-3-1")),
                                            MatchTime{59, std::nullopt})
                        .has_value());
        // Player 10 is the striker of 4-2-3-1 (slot 9).
        ASSERT_TRUE(stack.matchDay
                        .recordSubstitution(kMatchId, pid(12), pid(10), MatchTime{65, std::nullopt})
                        .has_value());
    }
    auto db = open();
    ASSERT_NE(db, nullptr);
    MatchDayStack stack(*db, m_clock);
    ASSERT_TRUE(stack.auth.login(coach, "1234").has_value());
    const auto state = stack.matchDay.state(kMatchId);
    ASSERT_TRUE(state.has_value()) << state.error().detail;
    EXPECT_EQ(state->shape.formation, "4-2-3-1");
    EXPECT_EQ(state->shape.pitchSlots[9].player, pid(12));
    EXPECT_EQ(state->shape.pitchSlots[9].position, Position::St);
    ASSERT_EQ(state->tacticChanges.size(), 1U);
    EXPECT_EQ(state->tacticChanges.front().minute, 59);
    const auto appearances = stack.matches.listAppearances(kMatchId);
    ASSERT_TRUE(appearances.has_value());
    const auto substitute = std::ranges::find(*appearances, pid(12), &Appearance::playerId);
    ASSERT_NE(substitute, appearances->end());
    EXPECT_EQ(substitute->position, Position::St);
}

// --- Review of P13b: the end of the match and "set clock" never go before a stored action ---

// Codex: without the clock, a tactic change at 60', then red cards entered late at 20' leave six
// players. The end is never before a stored action (as P13 for events): the match ends at 60', not
// at 20', so the change stays inside the match.
TEST_F(MatchDayServiceTest, AnEarlyEndIsNeverBeforeAStoredTacticChange) {
    logInAs(Role::Coach);
    saveLineup();
    ASSERT_TRUE(m_service->start(kMatch, kHalves, 4, true).has_value());
    ASSERT_TRUE(m_service
                    ->recordTacticChange(kMatch, changeShape(read().shape, *findFormation("4-3-3")),
                                         MatchTime{59, std::nullopt})
                    .has_value());
    for (const std::int64_t player : {1, 2, 4, 5, 6}) {
        ASSERT_TRUE(m_service->recordRed(kMatch, pid(player), MatchTime{19, std::nullopt}));
    }
    ASSERT_LT(onPitchCount(read().situation), kMinPlayersOnPitch);
    // The minute typed (20') is earlier than the change: the end is at the change.
    const auto ended = m_service->endMatch(kMatch, 19);
    ASSERT_TRUE(ended.has_value()) << ended.error().detail;
    const MatchDayState state = read();
    ASSERT_TRUE(state.clock.has_value());
    EXPECT_EQ(state.clock->endedEarlyAt, 59);
    EXPECT_EQ(finalMinute(*state.clock), 59);
    expectMinutes(3, 0, 59);
    ASSERT_EQ(state.tacticChanges.size(), 1U);
    EXPECT_LE(state.tacticChanges.front().minute, finalMinute(*state.clock));
}

TEST_F(MatchDayServiceTest, SetClockNeverGoesBeforeATacticChangeOrANote) {
    startMatch();
    advance();
    m_clock.advance(35min);
    ASSERT_TRUE(m_service
                    ->recordTacticChange(kMatch, changeShape(read().shape, *findFormation("4-3-3")),
                                         MatchTime{30, std::nullopt})
                    .has_value());
    // 10:00 would put the change at 31' in the future.
    expectError(m_service->setClock(kMatch, 10min), ErrorCode::InvalidInput, "clock_before_event");
    ASSERT_TRUE(m_service->setClock(kMatch, 31min).has_value());
    // A note counts too: at 32' (stored 31) it needs 31:00.
    ASSERT_TRUE(
        m_service->addNote(kMatch, MatchTime{31, std::nullopt}, std::nullopt, "Wiatr").has_value());
    expectError(m_service->setClock(kMatch, 30min), ErrorCode::InvalidInput, "clock_before_event");
    EXPECT_TRUE(m_service->setClock(kMatch, 31min).has_value());
}

TEST_F(MatchDayServiceTest, TheUndoOfSetClockNeverGoesBeforeATacticChange) {
    startMatch();
    advance();
    m_clock.advance(20min);
    // The clock shows 40:00; its undo would bring back 20:00.
    const auto before = m_service->setClock(kMatch, 40min);
    ASSERT_TRUE(before.has_value());
    ASSERT_TRUE(m_service
                    ->recordTacticChange(kMatch, changeShape(read().shape, *findFormation("4-3-3")),
                                         MatchTime{35, std::nullopt})
                    .has_value());
    expectError(m_service->restoreClock(kMatch, *before), ErrorCode::InvalidInput,
                "clock_before_event");
    const auto reading = readClock(*read().clock, m_clock.now());
    EXPECT_EQ(reading.regular, 40min);
}
