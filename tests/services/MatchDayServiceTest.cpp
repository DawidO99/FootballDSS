#include "services/MatchDayService.hpp"

#include "ServiceFixture.hpp"
#include "core/algorithms/Formations.hpp"
#include "data/repositories/AvailabilityRepository.hpp"
#include "data/repositories/ClubRepository.hpp"
#include "data/repositories/LineupRepository.hpp"
#include "data/repositories/MatchLiveRepository.hpp"
#include "data/repositories/MatchRepository.hpp"
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
        m_service = std::make_unique<MatchDayService>(*m_db, *m_matches, *m_live, *m_lineups,
                                                      *m_clubs, *m_opponents, *m_players,
                                                      *m_lineupService, m_session, m_clock);
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
        const auto started = m_service->start(kMatch, 45, 4);
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
        expectError(m_service->start(kMatch, 45, 4), ErrorCode::PermissionDenied);
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
        ASSERT_TRUE(m_service->start(kMatch, 45, 4).has_value());
        ASSERT_TRUE(m_service->cancelStart(kMatch).has_value());
    }
}

TEST_F(MatchDayServiceTest, StartCheckNamesWhatBlocksTheStart) {
    logInAs(Role::Coach);
    auto check = m_service->startCheck(kMatch, 4);
    ASSERT_TRUE(check.has_value()) << check.error().detail;
    EXPECT_EQ(check->block, MatchDayStartCheck::Block::NoLineup);
    EXPECT_EQ(check->defaultHalfLength, 45);
    expectError(m_service->start(kMatch, 45, 4), ErrorCode::Conflict, "no_lineup");

    saveLineup(kMatch, false);
    check = m_service->startCheck(kMatch, 4);
    EXPECT_EQ(check->block, MatchDayStartCheck::Block::LineupErrors);
    EXPECT_GT(check->lineupErrors, 0);
    expectError(m_service->start(kMatch, 45, 4), ErrorCode::Conflict, "lineup_errors");

    saveLineup();
    check = m_service->startCheck(kMatch, 4);
    EXPECT_EQ(check->block, MatchDayStartCheck::Block::None);
    EXPECT_EQ(check->lineupErrors, 0);

    EXPECT_EQ(m_service->startCheck(kFinished, 4)->block, MatchDayStartCheck::Block::NotPlanned);
    expectError(m_service->start(kFinished, 45, 4), ErrorCode::Conflict, "match_not_planned");

    expectError(m_service->start(kMatch, 9, 4), ErrorCode::InvalidInput, "half_length");
    expectError(m_service->start(kMatch, 61, 4), ErrorCode::InvalidInput, "half_length");
    ASSERT_TRUE(m_service->start(kMatch, 45, 4).has_value());
    ASSERT_TRUE(m_lineups->saveForMatch(kOtherPlanned, lineup()));
    EXPECT_EQ(m_service->startCheck(kOtherPlanned, 4)->block,
              MatchDayStartCheck::Block::AnotherLive);
    expectError(m_service->start(kOtherPlanned, 45, 4), ErrorCode::Conflict, "another_live_match");
}

TEST_F(MatchDayServiceTest, DefaultHalfLengthFollowsTheCategory) {
    logInAs(Role::Coach);
    ASSERT_TRUE(sql("UPDATE team SET category = 'u13' WHERE id = 1"));
    EXPECT_EQ(m_service->startCheck(kMatch, 4)->defaultHalfLength, 30);
}

TEST_F(MatchDayServiceTest, StartMakesTheMatchLiveWithStartersOnThePitch) {
    startMatch();
    EXPECT_EQ(m_matches->get(kMatch)->status, MatchStatus::Live);
    EXPECT_EQ(m_service->liveMatch(kTeam).value(), kMatch);
    const MatchDayState state = read();
    ASSERT_TRUE(state.clock.has_value());
    EXPECT_EQ(state.clock->halfLength, 45);
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

    ASSERT_TRUE(m_service->start(kMatch, 45, 4).has_value());
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
    // No cards or goals in the break.
    expectError(m_service->recordYellow(kMatch, pid(6)), ErrorCode::Conflict, "match_not_running");
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
    EXPECT_EQ(undone->step, ClockStep::FirstHalfEnd);
    EXPECT_EQ(phaseOf(*read().clock), MatchPhase::FirstHalf);

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
    EXPECT_EQ(undone->step, ClockStep::FirstHalfStart);
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
    EXPECT_EQ(undone->step, ClockStep::MatchEnd);
    const auto match = m_matches->get(kMatch);
    EXPECT_EQ(match->status, MatchStatus::Live);
    EXPECT_EQ(match->goalsFor, std::nullopt);
    EXPECT_EQ(match->goalsAgainst, std::nullopt);
    expectMinutes(9, 0, std::nullopt);
    EXPECT_EQ(phaseOf(*read().clock), MatchPhase::SecondHalf);
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
    EXPECT_EQ(reading.phase, MatchPhase::FirstHalf);
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
    for (const char* table :
         {"match", "match_event", "appearance", "match_live", "match_clock_step"}) {
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
    ASSERT_TRUE(m_service->start(kOtherPlanned, 45, 4).has_value());

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
    // The red row goes first; the failure comes at the yellow one.
    QString before = snapshot(*m_db);
    ASSERT_TRUE(sql("CREATE TEMP TRIGGER fail_undo BEFORE DELETE ON match_event "
                    "WHEN OLD.type = 'yellow' BEGIN SELECT RAISE(ABORT, 'forced'); END"));
    EXPECT_FALSE(m_service->undoLast(kMatch).has_value());
    EXPECT_EQ(snapshot(*m_db), before);
    ASSERT_TRUE(sql("DROP TRIGGER fail_undo"));

    // Undo of the end: the step goes first; the failure comes when the match is made live again.
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
        : staff(db), clubs(db), lineups(db), matches(db), live(db), opponents(db), players(db),
          availability(db), training(db), auth(db, staff, session, clock, 4),
          lineupService(db, lineups, matches, opponents, players, availability, training, session,
                        clock),
          matchDay(db, matches, live, lineups, clubs, opponents, players, lineupService, session,
                   clock) {}

    StaffRepository staff;
    ClubRepository clubs;
    LineupRepository lineups;
    MatchRepository matches;
    MatchLiveRepository live;
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
        ASSERT_TRUE(stack.matchDay.start(kMatchId, 45, 4).has_value());
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
    EXPECT_EQ(reading.phase, MatchPhase::FirstHalf);
    EXPECT_EQ(reading.regular, 25min);
    EXPECT_EQ(state->situation.goalsFor, 1);
    ASSERT_EQ(state->events.size(), 1U);
    EXPECT_EQ(state->events.front().minute, 12);
    ASSERT_TRUE(stack.matchDay.recordGoal(kMatchId, pid(10), std::nullopt).has_value());
    EXPECT_EQ(stack.matches.listEvents(kMatchId)->back().minute, 25);
    EXPECT_EQ(stack.matches.listAppearances(kMatchId)->size(), 11U);
}
