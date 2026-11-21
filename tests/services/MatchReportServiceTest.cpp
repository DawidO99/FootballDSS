#include "services/MatchReportService.hpp"

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
#include "services/DemoDataService.hpp"
#include "services/LineupService.hpp"
#include "services/LoadService.hpp"
#include "services/MatchDayService.hpp"

#include <QSqlQuery>
#include <QSqlRecord>
#include <QStringList>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <ranges>
#include <string>
#include <vector>

using namespace fdss::core;
using namespace fdss::data;
using namespace fdss::services;
using namespace fdss::tests;

namespace {

// Team 1 (senior) with players 1–14 and 15 (not in the lineup); match 1 today (FakeClock:
// 2026-11-01) with the lineup 4-4-2: goalkeeper 3 in slot 0, then 1, 2, 4, 5, …, 11; bench 12, 13,
// 14. Match 5 (2026-10-10) is a finished match without Match Day, filled in by the tests.
constexpr MatchId kOutside{5};
constexpr MatchFormat kHalves{2, 45};

PlayerId pid(std::int64_t id) { return PlayerId{id}; }

MatchTime at(int minute, std::optional<int> added = std::nullopt) { return {minute, added}; }

MatchEventDraft eventOf(MatchEventType type, std::optional<std::int64_t> player, int minute,
                        std::optional<std::int64_t> related = std::nullopt) {
    MatchEventDraft event;
    event.type = type;
    event.minute = minute;
    if (player) {
        event.playerId = pid(*player);
    }
    if (related) {
        event.relatedPlayerId = pid(*related);
    }
    return event;
}

// Every row the report writes, as text.
QString snapshot(const DatabaseManager& db) {
    QStringList rows;
    for (const char* table : {"match", "match_event", "appearance", "match_note",
                              "match_tactic_change", "match_live"}) {
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

class MatchReportServiceTest : public ServiceTest {
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
        for (int id = 3; id <= 15; ++id) {
            ASSERT_TRUE(sql("INSERT INTO player (id, team_id, first_name, last_name, "
                            "shirt_number, primary_position) VALUES (?, 1, 'P', ?, ?, ?)",
                            {id, QStringLiteral("Player%1").arg(id), id + 20,
                             id == 3 ? QStringLiteral("gk") : QStringLiteral("cm")}));
        }
        ASSERT_TRUE(sql("INSERT INTO match (id, team_id, opponent_id, date, venue, goals_for, "
                        "goals_against, status) VALUES (5, 1, 1, '2026-10-10', 'home', 1, 0, "
                        "'finished')"));
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
        m_matchDay = std::make_unique<MatchDayService>(
            *m_db, *m_matches, *m_live, *m_notes, *m_tactics, *m_lineups, *m_clubs, *m_opponents,
            *m_players, *m_lineupService, m_session, m_clock);
        m_service =
            std::make_unique<MatchReportService>(*m_db, *m_matches, *m_live, *m_notes, *m_tactics,
                                                 *m_lineups, *m_opponents, *m_players, m_session);
    }

    void logInAs(Role role) { logIn(addUser(std::string(toDbText(role)).c_str(), role)); }

    static Lineup lineup() {
        Lineup result = emptyLineup(*findFormation("4-4-2"));
        const std::int64_t players[] = {3, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11};
        for (std::size_t slot = 0; slot < result.pitchSlots.size(); ++slot) {
            result.pitchSlots[slot].player = pid(players[slot]);
        }
        result.bench = {pid(12), pid(13), pid(14)};
        return result;
    }

    // Coach logged in; match 1 played in Match Day without the clock: a goal of 9 at 20' and an
    // opponent goal at 80', then the end.
    void playMatch() {
        logInAs(Role::Coach);
        ASSERT_TRUE(m_lineups->saveForMatch(kMatch, lineup()));
        ASSERT_TRUE(m_matchDay->start(kMatch, kHalves, 4, true).has_value());
        ASSERT_TRUE(m_matchDay->recordGoal(kMatch, pid(9), pid(10), at(20)).has_value());
        ASSERT_TRUE(m_matchDay->recordOpponentGoal(kMatch, at(80)).has_value());
        const auto ended = m_matchDay->endMatch(kMatch);
        ASSERT_TRUE(ended.has_value()) << ended.error().detail;
    }

    // Match 5 as the demo data writes it (no Match Day): 11 starters, 12 on for 9 at 60', a goal
    // of 12 at 70'; 1:0. `finalMinute`: how long the match lasted; `startedFlag`: the starters have
    // `started` set (an import may give minute_on 0 only).
    void fillOutsideMatch(bool consistent = true, int finalMinute = 90, bool startedFlag = true) {
        const std::int64_t starters[] = {3, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11};
        std::vector<AppearanceDraft> rows;
        for (const std::int64_t player : starters) {
            AppearanceDraft row;
            row.playerId = pid(player);
            row.started = startedFlag;
            row.minuteOn = 0;
            row.minuteOff = player == 9 ? 60 : finalMinute;
            row.position = player == 3 ? Position::Gk : Position::Cb;
            row.rating = 6;
            rows.push_back(row);
        }
        AppearanceDraft sub;
        sub.playerId = pid(12);
        sub.minuteOn = 60;
        sub.minuteOff = consistent ? finalMinute : finalMinute - 10;
        sub.position = Position::St;
        rows.push_back(sub);
        ASSERT_TRUE(m_matches->replaceAppearances(kOutside, rows));
        ASSERT_TRUE(m_matches->appendEvent(kOutside, eventOf(MatchEventType::Sub, 12, 60, 9)));
        ASSERT_TRUE(m_matches->appendEvent(kOutside, eventOf(MatchEventType::Goal, 12, 70)));
    }

    [[nodiscard]] MatchReport read(MatchId match = kMatch) const {
        auto report = m_service->report(match);
        EXPECT_TRUE(report.has_value()) << report.error().detail;
        return report.value_or(MatchReport{});
    }

    [[nodiscard]] Appearance appearance(std::int64_t player, MatchId match = kMatch) const {
        const auto appearances = m_matches->listAppearances(match);
        EXPECT_TRUE(appearances.has_value());
        for (const Appearance& item : appearances.value_or(std::vector<Appearance>{})) {
            if (item.playerId == pid(player)) {
                return item;
            }
        }
        ADD_FAILURE() << "no appearance of player " << player;
        return {};
    }

    void expectMinutes(std::int64_t player, std::optional<int> on, std::optional<int> off,
                       MatchId match = kMatch) const {
        SCOPED_TRACE(player);
        const Appearance item = appearance(player, match);
        EXPECT_EQ(item.minuteOn, on);
        EXPECT_EQ(item.minuteOff, off);
    }

    [[nodiscard]] MatchEventId eventId(MatchEventType type, std::int64_t player,
                                       MatchId match = kMatch) const {
        const std::vector<MatchEvent> events = m_matches->listEvents(match).value();
        for (const MatchEvent& event : events) {
            if (event.type == type && event.playerId == pid(player)) {
                return event.id;
            }
        }
        ADD_FAILURE() << "no such event";
        return {};
    }

    [[nodiscard]] std::pair<int, int> score(MatchId match = kMatch) const {
        const Match stored = m_matches->get(match).value();
        return {stored.goalsFor.value_or(-1), stored.goalsAgainst.value_or(-1)};
    }

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
    std::unique_ptr<MatchDayService> m_matchDay;
    std::unique_ptr<MatchReportService> m_service;
};

} // namespace

// --- Reading and permissions ---

TEST_F(MatchReportServiceTest, ReadingNeedsALogin) {
    expectError(m_service->report(kMatch), ErrorCode::PermissionDenied);
    expectError(m_service->playerMatches(kPlayer), ErrorCode::PermissionDenied);
    expectError(m_service->situationAt(kMatch, at(10)), ErrorCode::PermissionDenied);
}

TEST_F(MatchReportServiceTest, TheReportOfAMatchFromMatchDay) {
    playMatch();
    const MatchReport report = read();
    EXPECT_TRUE(report.fromMatchDay);
    EXPECT_TRUE(report.correctable);
    EXPECT_EQ(report.match.opponentName, "Rywal");
    EXPECT_EQ(report.match.match.goalsFor, 1);
    EXPECT_EQ(report.match.match.goalsAgainst, 1);
    EXPECT_EQ(report.finalMinute, 90);
    EXPECT_EQ(report.format, kHalves);
    ASSERT_EQ(report.appearances.size(), 11U);
    EXPECT_EQ(report.appearances.front().playerId, pid(3)); // the order of the sheet
    EXPECT_EQ(report.events.size(), 2U);
    EXPECT_EQ(report.players.size(), 14U); // the bench too
    ASSERT_EQ(report.shapes.size(), 1U);
    EXPECT_EQ(report.shapes[0].formation, "4-4-2");
    EXPECT_EQ(report.shapes[0].to, std::nullopt);
}

TEST_F(MatchReportServiceTest, ChangesNeedEditLineupWhileEveryRoleReads) {
    playMatch();
    const MatchEventId goal = eventId(MatchEventType::Goal, 9);
    const std::vector<RatingEntry> ratings{{pid(9), 8, 7}};
    for (const Role role : {Role::Physio, Role::Analyst}) {
        SCOPED_TRACE(toDbText(role));
        logInAs(role);
        EXPECT_TRUE(m_service->report(kMatch).has_value());
        EXPECT_TRUE(m_service->playerMatches(pid(9)).has_value());
        expectError(m_service->save(kMatch, ratings, "x"), ErrorCode::PermissionDenied);
        expectError(m_service->addEvent(kMatch, eventOf(MatchEventType::Yellow, 4, 30)),
                    ErrorCode::PermissionDenied);
        expectError(m_service->deleteEvent(kMatch, goal), ErrorCode::PermissionDenied);
        expectError(m_service->addNote(kMatch, at(10), std::nullopt, "x"),
                    ErrorCode::PermissionDenied);
        expectError(m_service->deleteTacticChange(kMatch, MatchTacticChangeId{1}),
                    ErrorCode::PermissionDenied);
    }
    logInAs(Role::Assistant);
    EXPECT_TRUE(m_service->save(kMatch, ratings, "x").has_value());
}

// --- Ratings, RPE and note ---

TEST_F(MatchReportServiceTest, SaveStoresRatingsRpeAndTheNote) {
    playMatch();
    const std::vector<RatingEntry> ratings{{pid(9), 10, 8}, {pid(3), 1, 0}, {pid(4), {}, 5}};
    const auto saved = m_service->save(kMatch, ratings, "  Dobry pressing\nw drugiej połowie ");
    ASSERT_TRUE(saved.has_value()) << saved.error().detail;
    EXPECT_EQ(appearance(9).rating, 10);
    EXPECT_EQ(appearance(9).rpe, 8);
    EXPECT_EQ(appearance(3).rating, 1);
    EXPECT_EQ(appearance(3).rpe, 0);
    EXPECT_EQ(appearance(4).rating, std::nullopt);
    EXPECT_EQ(appearance(4).rpe, 5);
    EXPECT_EQ(read().reportNote, std::string("Dobry pressing\nw drugiej połowie"));
    ASSERT_TRUE(m_service->save(kMatch, {}, "   ").has_value());
    EXPECT_EQ(read().reportNote, std::nullopt);
    EXPECT_EQ(appearance(9).rating, 10); // rows not given stay
}

TEST_F(MatchReportServiceTest, RatingAndRpeRangesAndNothingWrittenOnAnError) {
    playMatch();
    const QString before = snapshot(*m_db);
    const auto attempt = [&](std::optional<int> rating, std::optional<int> rpe,
                             std::int64_t player = 9) {
        const std::vector<RatingEntry> ratings{{pid(4), 7, 7}, {pid(player), rating, rpe}};
        return m_service->save(kMatch, ratings, "note");
    };
    expectError(attempt(0, 5), ErrorCode::InvalidInput, "rating");
    expectError(attempt(11, 5), ErrorCode::InvalidInput, "rating");
    expectError(attempt(5, -1), ErrorCode::InvalidInput, "rpe");
    expectError(attempt(5, 11), ErrorCode::InvalidInput, "rpe");
    expectError(attempt(5, 5, 12), ErrorCode::InvalidInput, "player_unknown"); // bench only
    expectError(m_service->save(kMatch, {}, std::string(2001, 'x')), ErrorCode::InvalidInput,
                "report_note");
    EXPECT_EQ(snapshot(*m_db), before);
    EXPECT_TRUE(m_service->save(kMatch, {}, std::string(2000, 'x')).has_value());
}

TEST_F(MatchReportServiceTest, OnlyAFinishedMatchHasAReportToChange) {
    logInAs(Role::Coach);
    const MatchReport planned = read();
    EXPECT_FALSE(planned.correctable);
    expectError(m_service->save(kMatch, {}, "x"), ErrorCode::Conflict, "match_not_finished");
    expectError(m_service->addNote(kMatch, at(10), std::nullopt, "x"), ErrorCode::Conflict,
                "match_not_finished");
    expectError(m_service->report(MatchId{999}), ErrorCode::NotFound);
}

TEST_F(MatchReportServiceTest, RecomputingTheMinutesKeepsRatingsAndRpe) {
    playMatch();
    const std::vector<RatingEntry> ratings{{pid(9), 8, 7}};
    ASSERT_TRUE(m_service->save(kMatch, ratings, ""));
    ASSERT_TRUE(m_service->addEvent(kMatch, eventOf(MatchEventType::Sub, 12, 60, 9)).has_value());
    EXPECT_EQ(appearance(9).rating, 8);
    EXPECT_EQ(appearance(9).rpe, 7);
}

// --- Corrections ---

TEST_F(MatchReportServiceTest, AForgottenSubstitutionChangesTheMinutes) {
    playMatch();
    const auto added = m_service->addEvent(kMatch, eventOf(MatchEventType::Sub, 12, 60, 9));
    ASSERT_TRUE(added.has_value()) << added.error().detail;
    expectMinutes(9, 0, 60);
    expectMinutes(12, 60, 90);
    EXPECT_EQ(appearance(12).position, lineup().pitchSlots[8].position); // the slot of 9
    const auto event = m_matches->listEvents(kMatch)->at(1);
    EXPECT_EQ(event.type, MatchEventType::Sub);
    EXPECT_EQ(event.seq, std::nullopt); // not an action of Match Day
    EXPECT_EQ(read().appearances.size(), 12U);
}

TEST_F(MatchReportServiceTest, GoalsAddedAndDeletedChangeTheScore) {
    playMatch();
    ASSERT_TRUE(m_service->addEvent(kMatch, eventOf(MatchEventType::Goal, 7, 55, 9)).has_value());
    EXPECT_EQ(score(), std::pair(2, 1));
    const MatchEventId opponent = m_matches->listEvents(kMatch)->back().id;
    const auto deleted = m_service->deleteEvent(kMatch, opponent);
    ASSERT_TRUE(deleted.has_value()) << deleted.error().detail;
    EXPECT_EQ(score(), std::pair(2, 0));
    EXPECT_EQ(m_matches->get(kMatch)->status, MatchStatus::Finished);
}

TEST_F(MatchReportServiceTest, TheMinuteMustBeInsideTheMatch) {
    playMatch();
    expectError(m_service->addEvent(kMatch, eventOf(MatchEventType::Yellow, 4, 91)),
                ErrorCode::InvalidInput, "minute_out_of_match");
    MatchEventDraft late = eventOf(MatchEventType::Yellow, 4, 90);
    late.addedTime = 31;
    expectError(m_service->addEvent(kMatch, late), ErrorCode::InvalidInput, "minute_out_of_match");
    late.addedTime = 3; // 90+3' is stoppage time of the match
    EXPECT_TRUE(m_service->addEvent(kMatch, late).has_value());
}

TEST_F(MatchReportServiceTest, AnEventThatALaterOneNoLongerFitsIsRefused) {
    playMatch();
    const MatchEventDraft red = eventOf(MatchEventType::Red, 9, 10);
    const auto conflict = m_service->checkEvent(kMatch, red);
    ASSERT_TRUE(conflict.has_value());
    ASSERT_TRUE(conflict->has_value());
    ASSERT_TRUE((*conflict)->laterEvent.has_value());
    EXPECT_EQ((*conflict)->laterEvent->type, MatchEventType::Goal);
    expectError(m_service->addEvent(kMatch, red), ErrorCode::Conflict, "event_conflict");
    expectError(m_service->addEvent(kMatch, eventOf(MatchEventType::Sub, 4, 30, 5)),
                ErrorCode::InvalidInput, "player_not_on_bench");
    EXPECT_EQ(m_service->checkEvent(kMatch, eventOf(MatchEventType::Yellow, 9, 30)).value(),
              std::nullopt);
}

TEST_F(MatchReportServiceTest, ASecondYellowBringsItsRedAndGoesWithIt) {
    playMatch();
    ASSERT_TRUE(m_service->addEvent(kMatch, eventOf(MatchEventType::Yellow, 4, 30)).has_value());
    const auto second = m_service->addEvent(kMatch, eventOf(MatchEventType::Yellow, 4, 50));
    ASSERT_TRUE(second.has_value()) << second.error().detail;
    EXPECT_EQ(second->size(), 2U);
    expectMinutes(4, 0, 50);
    // Deleting the red of the second yellow takes the yellow with it.
    const MatchEventId red = eventId(MatchEventType::Red, 4);
    const auto deleted = m_service->deleteEvent(kMatch, red);
    ASSERT_TRUE(deleted.has_value()) << deleted.error().detail;
    EXPECT_EQ(deleted->events.size(), 2U);
    expectMinutes(4, 0, 90);
    EXPECT_EQ(m_matches->listEvents(kMatch)->size(), 3U); // goal, first yellow, opponent goal
}

TEST_F(MatchReportServiceTest, DeletingWhatALaterEventNeedsIsRefused) {
    playMatch();
    ASSERT_TRUE(m_service->addEvent(kMatch, eventOf(MatchEventType::Sub, 12, 60, 9)).has_value());
    ASSERT_TRUE(m_service->addEvent(kMatch, eventOf(MatchEventType::Goal, 12, 70)).has_value());
    const MatchEventId sub = eventId(MatchEventType::Sub, 12);
    const auto dependent = m_service->checkDeleteEvent(kMatch, sub);
    ASSERT_TRUE(dependent.has_value());
    ASSERT_TRUE(dependent->has_value());
    EXPECT_EQ((*dependent)->type, MatchEventType::Goal);
    EXPECT_EQ((*dependent)->playerId, pid(12));
    expectError(m_service->deleteEvent(kMatch, sub), ErrorCode::Conflict, "event_dependent");

    // The first yellow of a second yellow: the red would turn into a straight red.
    ASSERT_TRUE(m_service->addEvent(kMatch, eventOf(MatchEventType::Yellow, 4, 30)).has_value());
    ASSERT_TRUE(m_service->addEvent(kMatch, eventOf(MatchEventType::Yellow, 4, 50)).has_value());
    // goal 20', yellow 30', yellow 50', red 50', sub 60'…
    const MatchEvent first = m_matches->listEvents(kMatch)->at(1);
    ASSERT_EQ(first.type, MatchEventType::Yellow);
    ASSERT_EQ(first.minute, 30);
    expectError(m_service->deleteEvent(kMatch, first.id), ErrorCode::Conflict, "event_dependent");
    expectError(m_service->deleteEvent(kMatch, MatchEventId{999}), ErrorCode::NotFound);
}

TEST_F(MatchReportServiceTest, ARestoredEventKeepsItsIdAndSeq) {
    playMatch();
    const MatchEvent goal = m_matches->listEvents(kMatch)->front();
    ASSERT_TRUE(goal.seq.has_value());
    const auto deleted = m_service->deleteEvent(kMatch, goal.id);
    ASSERT_TRUE(deleted.has_value());
    EXPECT_EQ(score(), std::pair(0, 1));
    const auto restored = m_service->restoreEvents(kMatch, deleted->events, deleted->ratings);
    ASSERT_TRUE(restored.has_value()) << restored.error().detail;
    EXPECT_EQ(m_matches->listEvents(kMatch)->front(), goal);
    EXPECT_EQ(score(), std::pair(1, 1));
}

TEST_F(MatchReportServiceTest, ACorrectionIsAtomic) {
    playMatch();
    const QString before = snapshot(*m_db);
    ASSERT_TRUE(sql("CREATE TEMP TRIGGER fail_minutes BEFORE INSERT ON appearance "
                    "BEGIN SELECT RAISE(ABORT, 'forced'); END"));
    EXPECT_FALSE(m_service->addEvent(kMatch, eventOf(MatchEventType::Sub, 12, 60, 9)));
    EXPECT_FALSE(m_service->deleteEvent(kMatch, eventId(MatchEventType::Goal, 9)));
    EXPECT_EQ(snapshot(*m_db), before);
    ASSERT_TRUE(sql("DROP TRIGGER fail_minutes"));
    ASSERT_TRUE(sql("CREATE TEMP TRIGGER fail_score BEFORE UPDATE ON match "
                    "BEGIN SELECT RAISE(ABORT, 'forced'); END"));
    EXPECT_FALSE(m_service->addEvent(kMatch, eventOf(MatchEventType::Goal, 7, 55)));
    EXPECT_EQ(snapshot(*m_db), before);
    ASSERT_TRUE(sql("DROP TRIGGER fail_score"));
    EXPECT_TRUE(m_service->addEvent(kMatch, eventOf(MatchEventType::Goal, 7, 55)));
}

TEST_F(MatchReportServiceTest, UndoInMatchDayStillTakesBackTheEnd) {
    playMatch();
    ASSERT_TRUE(m_service->addEvent(kMatch, eventOf(MatchEventType::Yellow, 4, 30)).has_value());
    const auto undone = m_matchDay->undoLast(kMatch);
    ASSERT_TRUE(undone.has_value()) << undone.error().detail;
    EXPECT_EQ(undone->kind, MatchDayAction::Kind::Finish);
    EXPECT_EQ(m_matches->get(kMatch)->status, MatchStatus::Live);
    expectError(m_service->addEvent(kMatch, eventOf(MatchEventType::Yellow, 5, 30)),
                ErrorCode::Conflict, "match_not_finished");
}

// --- A match without Match Day (P14 decision 2) ---

TEST_F(MatchReportServiceTest, AMatchWithoutMatchDayIsCorrectedOnItsRebuiltSheet) {
    logInAs(Role::Coach);
    fillOutsideMatch();
    const MatchReport report = read(kOutside);
    EXPECT_FALSE(report.fromMatchDay);
    EXPECT_TRUE(report.correctable);
    EXPECT_EQ(report.finalMinute, 90);
    EXPECT_EQ(report.format, std::nullopt); // no format is guessed
    EXPECT_EQ(report.appearances.size(), 12U);
    EXPECT_TRUE(report.shapes.empty());
    // 15 never played: the rest of the squad may come on in a correction.
    const auto added = m_service->addEvent(kOutside, eventOf(MatchEventType::Sub, 15, 75, 4));
    ASSERT_TRUE(added.has_value()) << added.error().detail;
    expectMinutes(4, 0, 75, kOutside);
    expectMinutes(15, 75, 90, kOutside);
    expectMinutes(12, 60, 90, kOutside);
    EXPECT_EQ(appearance(12, kOutside).position, Position::St); // the stored position stays
    EXPECT_EQ(appearance(1, kOutside).rating, 6);
    EXPECT_EQ(score(kOutside), std::pair(1, 0));
    EXPECT_TRUE(read(kOutside).correctable);
}

TEST_F(MatchReportServiceTest, AnInconsistentMatchHasRatingsButNoCorrections) {
    logInAs(Role::Coach);
    fillOutsideMatch(false); // 12 stored until 80', the events say 90'
    const MatchReport report = read(kOutside);
    EXPECT_FALSE(report.correctable);
    expectError(m_service->addEvent(kOutside, eventOf(MatchEventType::Yellow, 4, 30)),
                ErrorCode::Conflict, "not_correctable");
    expectError(m_service->deleteEvent(kOutside, eventId(MatchEventType::Goal, 12, kOutside)),
                ErrorCode::Conflict, "not_correctable");
    const std::vector<RatingEntry> ratings{{pid(12), 9, 6}};
    EXPECT_TRUE(m_service->save(kOutside, ratings, "Mecz z importu").has_value());
    EXPECT_TRUE(m_service->addNote(kOutside, at(30), pid(12), "Uraz").has_value());
}

TEST_F(MatchReportServiceTest, AStarterIsTheOneOnFromMinuteZero) {
    logInAs(Role::Coach);
    fillOutsideMatch(true, 90, false); // started = 0, minute_on = 0
    const MatchReport report = read(kOutside);
    EXPECT_TRUE(report.correctable);
    EXPECT_EQ(report.sheet.starters.size(), 11U);
    const auto added = m_service->addEvent(kOutside, eventOf(MatchEventType::Sub, 15, 75, 4));
    ASSERT_TRUE(added.has_value()) << added.error().detail;
    expectMinutes(4, 0, 75, kOutside);
    expectMinutes(15, 75, 90, kOutside);
}

TEST_F(MatchReportServiceTest, AMatchWithoutMatchDayTakesEveryMinuteOfItsData) {
    logInAs(Role::Coach);
    for (const int finalMinute : {95, 101, 115}) {
        SCOPED_TRACE(finalMinute);
        ASSERT_TRUE(sql("DELETE FROM match_event WHERE match_id = 5"));
        fillOutsideMatch(true, finalMinute);
        const MatchReport report = read(kOutside);
        ASSERT_TRUE(report.correctable);
        EXPECT_EQ(report.finalMinute, finalMinute);
        EXPECT_EQ(report.format, std::nullopt);
        const auto late = m_service->addEvent(
            kOutside, eventOf(MatchEventType::OpponentGoal, std::nullopt, finalMinute - 1));
        ASSERT_TRUE(late.has_value()) << late.error().detail;
        EXPECT_EQ(score(kOutside), std::pair(1, 1));
        expectError(m_service->addEvent(kOutside, eventOf(MatchEventType::OpponentGoal,
                                                          std::nullopt, finalMinute + 1)),
                    ErrorCode::InvalidInput, "minute_out_of_match");
        MatchEventDraft added = eventOf(MatchEventType::OpponentGoal, std::nullopt, 45);
        added.addedTime = 2;
        expectError(m_service->addEvent(kOutside, added), ErrorCode::InvalidInput,
                    "minute_out_of_match");
        expectMinutes(12, 60, finalMinute, kOutside);
        ASSERT_TRUE(sql("UPDATE match SET goals_against = 0 WHERE id = 5"));
    }
}

TEST_F(MatchReportServiceTest, UndoOfADeletedSubstitutionBringsBackItsRating) {
    playMatch();
    ASSERT_TRUE(m_service->addEvent(kMatch, eventOf(MatchEventType::Sub, 12, 60, 9)).has_value());
    const std::vector<RatingEntry> ratings{{pid(12), 8, 6}, {pid(9), 5, 7}};
    ASSERT_TRUE(m_service->save(kMatch, ratings, ""));
    const auto deleted = m_service->deleteEvent(kMatch, eventId(MatchEventType::Sub, 12));
    ASSERT_TRUE(deleted.has_value()) << deleted.error().detail;
    const std::vector<RatingEntry> taken{{pid(12), 8, 6}};
    EXPECT_EQ(deleted->ratings, taken);
    EXPECT_EQ(read().appearances.size(), 11U);
    EXPECT_EQ(appearance(9).rating, 5); // the player who stayed keeps his
    const auto restored = m_service->restoreEvents(kMatch, deleted->events, deleted->ratings);
    ASSERT_TRUE(restored.has_value()) << restored.error().detail;
    EXPECT_EQ(appearance(12).rating, 8);
    EXPECT_EQ(appearance(12).rpe, 6);
    expectMinutes(12, 60, 90);
}

TEST_F(MatchReportServiceTest, AFailedUndoOfASubstitutionRestoresNothing) {
    playMatch();
    ASSERT_TRUE(m_service->addEvent(kMatch, eventOf(MatchEventType::Sub, 12, 60, 9)).has_value());
    const std::vector<RatingEntry> ratings{{pid(12), 8, 6}};
    ASSERT_TRUE(m_service->save(kMatch, ratings, ""));
    const auto deleted = m_service->deleteEvent(kMatch, eventId(MatchEventType::Sub, 12));
    ASSERT_TRUE(deleted.has_value());
    const QString before = snapshot(*m_db);
    ASSERT_TRUE(sql("CREATE TEMP TRIGGER fail_rating BEFORE UPDATE OF rating ON appearance "
                    "BEGIN SELECT RAISE(ABORT, 'forced'); END"));
    EXPECT_FALSE(m_service->restoreEvents(kMatch, deleted->events, deleted->ratings));
    EXPECT_EQ(snapshot(*m_db), before);
}

// --- Notes and changes of shape ---

TEST_F(MatchReportServiceTest, NotesAreAddedEditedDeletedAndRestored) {
    playMatch();
    const auto id = m_service->addNote(kMatch, at(45, 2), pid(9), "  Kontuzja kostki ");
    ASSERT_TRUE(id.has_value()) << id.error().detail;
    const MatchNote note = m_notes->get(kMatch, *id).value();
    EXPECT_EQ(note.text, "Kontuzja kostki");
    EXPECT_EQ(note.addedTime, 2);
    EXPECT_EQ(note.seq, std::nullopt);
    ASSERT_TRUE(m_service->updateNote(kMatch, *id, at(50), std::nullopt, "Wiatr"));
    EXPECT_EQ(m_notes->get(kMatch, *id)->minute, 50);
    expectError(m_service->addNote(kMatch, at(10), std::nullopt, "  "), ErrorCode::InvalidInput,
                "note_text");
    expectError(m_service->addNote(kMatch, at(10), pid(15), "x"), ErrorCode::InvalidInput,
                "player_unknown");
    expectError(m_service->addNote(kMatch, at(95), std::nullopt, "x"), ErrorCode::InvalidInput,
                "minute_out_of_match");
    const auto deleted = m_service->deleteNote(kMatch, *id);
    ASSERT_TRUE(deleted.has_value());
    EXPECT_TRUE(m_notes->list(kMatch)->empty());
    ASSERT_TRUE(m_service->restoreNote(*deleted).has_value());
    EXPECT_EQ(m_notes->list(kMatch)->size(), 1U);
}

TEST_F(MatchReportServiceTest, DeletingAChangeOfShapeRecomputesThePositions) {
    logInAs(Role::Coach);
    ASSERT_TRUE(m_lineups->saveForMatch(kMatch, lineup()));
    ASSERT_TRUE(m_matchDay->start(kMatch, kHalves, 4, true).has_value());
    const Lineup shape = emptyLineup(*findFormation("4-3-3"));
    ASSERT_TRUE(m_matchDay->recordTacticChange(kMatch, shape, at(55)).has_value());
    ASSERT_TRUE(m_matchDay->recordSubstitution(kMatch, pid(12), pid(9), at(70)).has_value());
    ASSERT_TRUE(m_matchDay->endMatch(kMatch).has_value());
    const Position changed = *appearance(12).position;
    const MatchReport report = read();
    ASSERT_EQ(report.shapes.size(), 2U);
    EXPECT_EQ(report.shapes[0].formation, "4-4-2");
    EXPECT_EQ(report.shapes[0].to, at(55));
    EXPECT_EQ(report.shapes[1].from, at(55));
    EXPECT_EQ(report.shapes[1].formation, "4-3-3");

    const auto deleted = m_service->deleteTacticChange(kMatch, report.tacticChanges[0].id);
    ASSERT_TRUE(deleted.has_value()) << deleted.error().detail;
    EXPECT_EQ(appearance(12).position, lineup().pitchSlots[8].position); // the slot of 9
    EXPECT_EQ(read().shapes.size(), 1U);
    ASSERT_TRUE(m_service->restoreTacticChange(*deleted).has_value());
    EXPECT_EQ(appearance(12).position, changed);
    EXPECT_EQ(m_tactics->list(kMatch)->front().seq, deleted->seq);
    expectError(m_service->deleteTacticChange(kMatch, MatchTacticChangeId{999}),
                ErrorCode::NotFound);
}

// --- The "Matches" tab and the load ---

TEST_F(MatchReportServiceTest, PlayerMatchesAreTheFinishedOnesWithTheirStatistics) {
    playMatch();
    fillOutsideMatch();
    ASSERT_TRUE(m_service->addEvent(kMatch, eventOf(MatchEventType::Yellow, 9, 30)).has_value());
    const std::vector<RatingEntry> ratings{{pid(9), 8, 6}};
    ASSERT_TRUE(m_service->save(kMatch, ratings, ""));
    const auto rows = m_service->playerMatches(pid(9));
    ASSERT_TRUE(rows.has_value()) << rows.error().detail;
    ASSERT_EQ(rows->size(), 2U);
    const PlayerMatchRow& today = rows->at(0);
    EXPECT_EQ(today.match, kMatch);
    EXPECT_EQ(today.opponentName, "Rywal");
    EXPECT_EQ(today.goalsFor, 1);
    EXPECT_EQ(today.goalsAgainst, 1);
    EXPECT_EQ(today.minutes, 90);
    EXPECT_EQ(today.goals, 1);
    EXPECT_EQ(today.yellowCards, 1);
    EXPECT_EQ(today.rating, 8);
    EXPECT_EQ(today.rpe, 6);
    EXPECT_EQ(rows->at(1).match, kOutside);
    EXPECT_EQ(rows->at(1).minutes, 60);
    const auto assistant = m_service->playerMatches(pid(10));
    ASSERT_TRUE(assistant.has_value());
    EXPECT_EQ(assistant->at(0).assists, 1);
    EXPECT_TRUE(m_service->playerMatches(pid(15))->empty());
}

TEST_F(MatchReportServiceTest, TheMatchRpeChangesTheAcuteLoad) {
    playMatch();
    LoadService load(*m_players, *m_training, *m_matches, m_session, m_clock);
    const auto acute = [&] {
        auto future = load.playerLoad(pid(9));
        EXPECT_TRUE(future.has_value());
        return future ? future->result().load.assessment.indicators.acute : -1.0;
    };
    const double before = acute();
    const std::vector<RatingEntry> ratings{{pid(9), 7, 8}};
    ASSERT_TRUE(m_service->save(kMatch, ratings, ""));
    const double after = acute();
    EXPECT_DOUBLE_EQ(before, 0.0);
    // One day of 8 × 90 = 720: the first day of the series starts the EWMA.
    EXPECT_DOUBLE_EQ(after, 720.0);
}

// --- The demo data (P10): matches without Match Day ---

namespace {

class MatchReportDemoTest : public ServiceTest {
protected:
    void SetUp() override {
        ServiceTest::SetUp();
        if (HasFatalFailure()) {
            return;
        }
        seedTeams();
        m_players = std::make_unique<PlayerRepository>(*m_db);
        m_availability = std::make_unique<AvailabilityRepository>(*m_db);
        m_opponents = std::make_unique<OpponentRepository>(*m_db);
        m_matches = std::make_unique<MatchRepository>(*m_db);
        m_training = std::make_unique<TrainingRepository>(*m_db);
        m_lineups = std::make_unique<LineupRepository>(*m_db);
        m_live = std::make_unique<MatchLiveRepository>(*m_db);
        m_notes = std::make_unique<MatchNoteRepository>(*m_db);
        m_tactics = std::make_unique<MatchTacticRepository>(*m_db);
        DemoDataService demo(*m_db, *m_players, *m_availability, *m_opponents, *m_matches,
                             *m_training, *m_staff, *m_auth, m_session, m_clock);
        logIn(addUser("Jan Trener", Role::Coach));
        const auto loaded = demo.load(kTeam);
        ASSERT_TRUE(loaded.has_value()) << loaded.error().detail;
        m_service =
            std::make_unique<MatchReportService>(*m_db, *m_matches, *m_live, *m_notes, *m_tactics,
                                                 *m_lineups, *m_opponents, *m_players, m_session);
    }

    std::unique_ptr<PlayerRepository> m_players;
    std::unique_ptr<AvailabilityRepository> m_availability;
    std::unique_ptr<OpponentRepository> m_opponents;
    std::unique_ptr<MatchRepository> m_matches;
    std::unique_ptr<TrainingRepository> m_training;
    std::unique_ptr<LineupRepository> m_lineups;
    std::unique_ptr<MatchLiveRepository> m_live;
    std::unique_ptr<MatchNoteRepository> m_notes;
    std::unique_ptr<MatchTacticRepository> m_tactics;
    std::unique_ptr<MatchReportService> m_service;
};

} // namespace

TEST_F(MatchReportDemoTest, EveryFinishedDemoMatchCanBeCorrected) {
    const auto matches = m_matches->list(kTeam, {});
    ASSERT_TRUE(matches.has_value());
    int finished = 0;
    for (const Match& match : *matches) {
        if (match.status != MatchStatus::Finished) {
            continue;
        }
        ++finished;
        const auto report = m_service->report(match.id);
        ASSERT_TRUE(report.has_value()) << report.error().detail;
        EXPECT_TRUE(report->correctable) << "match " << match.id.value;
        EXPECT_FALSE(report->fromMatchDay);
    }
    EXPECT_GT(finished, 0);
}

TEST_F(MatchReportDemoTest, AForgottenSubstitutionInADemoMatchChangesTheMinutes) {
    const auto matches = m_matches->list(kTeam, {});
    ASSERT_TRUE(matches.has_value());
    const auto reversed = *matches | std::views::reverse;
    const auto last = std::ranges::find(reversed, MatchStatus::Finished, &Match::status);
    ASSERT_NE(last, reversed.end());
    const MatchId match = last->id;
    const MatchReport report = m_service->report(match).value();
    // A starter on the pitch until the end, and a player of the squad who did not play.
    const auto stayed = std::ranges::find_if(report.appearances, [](const Appearance& row) {
        return row.started && row.minuteOff == 90 && row.position != Position::Gk;
    });
    ASSERT_NE(stayed, report.appearances.end());
    ASSERT_FALSE(report.sheet.bench.empty());
    const PlayerId unused = report.sheet.bench.back();
    ASSERT_EQ(std::ranges::find(report.appearances, unused, &AppearanceDraft::playerId),
              report.appearances.end());
    const PlayerId off = stayed->playerId;
    const std::optional<int> rating = stayed->rating;

    MatchEventDraft sub;
    sub.type = MatchEventType::Sub;
    sub.minute = 85;
    sub.playerId = unused;
    sub.relatedPlayerId = off;
    const auto added = m_service->addEvent(match, sub);
    ASSERT_TRUE(added.has_value()) << added.error().detail;
    const auto rows = m_matches->listAppearances(match).value();
    const auto find = [&](PlayerId player) {
        return *std::ranges::find(rows, player, &AppearanceDraft::playerId);
    };
    EXPECT_EQ(find(off).minuteOff, 85);
    EXPECT_EQ(find(off).rating, rating);
    EXPECT_EQ(find(unused).minuteOn, 85);
    EXPECT_EQ(find(unused).minuteOff, 90);
    EXPECT_EQ(rows.size(), report.appearances.size() + 1);
    EXPECT_TRUE(m_service->report(match)->correctable);
}
