#include "services/CalendarService.hpp"

#include "ServiceFixture.hpp"
#include "core/algorithms/Formations.hpp"
#include "core/domain/Permissions.hpp"
#include "core/domain/Validation.hpp"
#include "data/repositories/LineupRepository.hpp"
#include "data/repositories/MatchRepository.hpp"
#include "data/repositories/OpponentRepository.hpp"
#include "data/repositories/TrainingRepository.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace fdss::core;
using namespace fdss::data;
using namespace fdss::services;
using namespace fdss::tests;

namespace {

constexpr Date kDay{std::chrono::year{2026}, std::chrono::November, std::chrono::day{7}};

class CalendarServiceTest : public ServiceTest {
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
        m_opponents = std::make_unique<OpponentRepository>(*m_db);
        m_matches = std::make_unique<MatchRepository>(*m_db);
        m_training = std::make_unique<TrainingRepository>(*m_db);
        m_lineups = std::make_unique<LineupRepository>(*m_db);
        m_calendar = std::make_unique<CalendarService>(*m_db, *m_opponents, *m_matches, *m_training,
                                                       *m_lineups, m_session);
    }

    void logInAs(Role role) {
        const StaffUserId id = addUser(std::string(toDbText(role)).c_str(), role);
        logIn(id);
    }

    static MatchDraft matchDraft() {
        MatchDraft draft;
        draft.opponentId = kOpponent;
        draft.date = kDay;
        draft.kickoffTime = std::chrono::hours{18};
        draft.venue = Venue::Away;
        draft.competition = "Liga";
        return draft;
    }

    static TrainingSessionDraft sessionDraft(int durationMin = 90) {
        TrainingSessionDraft draft;
        draft.date = kDay;
        draft.durationMin = durationMin;
        draft.type = SessionType::Recovery;
        draft.note = "Lekko";
        return draft;
    }

    static OpponentDraft opponentDraft(const char* name) {
        OpponentDraft draft;
        draft.name = name;
        return draft;
    }

    void seedAttendance() {
        AttendanceDraft present;
        present.playerId = kPlayer;
        present.rpe = 7;
        present.minutesOverride = 60;
        AttendanceDraft absent;
        absent.playerId = kOtherPlayer;
        absent.status = AttendanceStatus::Absent;
        const std::vector<AttendanceDraft> rows{present, absent};
        ASSERT_TRUE(m_training->saveAttendance(kSession, rows).has_value());
    }

    std::unique_ptr<OpponentRepository> m_opponents;
    std::unique_ptr<MatchRepository> m_matches;
    std::unique_ptr<TrainingRepository> m_training;
    std::unique_ptr<LineupRepository> m_lineups;
    std::unique_ptr<CalendarService> m_calendar;
};

struct GuardedCall {
    const char* name;
    std::function<Status(CalendarService&)> run;
};

template <class T> Status asStatus(const Result<T>& result) {
    return result ? Status{} : Status{std::unexpected(result.error())};
}

std::vector<GuardedCall> everyEditCall() {
    return {
        {"addOpponent",
         [](CalendarService& service) {
             OpponentDraft draft;
             draft.name = "Nowy";
             return asStatus(service.addOpponent(draft));
         }},
        {"addMatch",
         [](CalendarService& service) {
             MatchDraft draft;
             draft.opponentId = OpponentId{1};
             draft.date = kDay;
             return asStatus(service.addMatch(TeamId{1}, draft));
         }},
        {"updateMatch",
         [](CalendarService& service) {
             MatchDraft draft;
             draft.opponentId = OpponentId{1};
             draft.date = kDay;
             return service.updateMatch(MatchId{1}, draft);
         }},
        {"deleteMatch",
         [](CalendarService& service) { return asStatus(service.deleteMatch(MatchId{1})); }},
        {"restoreMatch",
         [](CalendarService& service) {
             DeletedMatch deleted;
             deleted.match.teamId = TeamId{1};
             deleted.match.opponentId = OpponentId{1};
             deleted.match.date = kDay;
             return asStatus(service.restoreMatch(deleted));
         }},
        {"addSession",
         [](CalendarService& service) {
             TrainingSessionDraft draft;
             draft.date = kDay;
             return asStatus(service.addSession(TeamId{1}, draft));
         }},
        {"updateSession",
         [](CalendarService& service) {
             TrainingSessionDraft draft;
             draft.date = kDay;
             return service.updateSession(TrainingSessionId{1}, draft);
         }},
        {"deleteSession",
         [](CalendarService& service) {
             return asStatus(service.deleteSession(TrainingSessionId{1}));
         }},
        {"restoreSession",
         [](CalendarService& service) {
             DeletedSession deleted;
             deleted.session.teamId = TeamId{1};
             deleted.session.date = kDay;
             return asStatus(service.restoreSession(deleted));
         }},
    };
}

std::vector<GuardedCall> everyReadCall() {
    return {
        {"listItems",
         [](CalendarService& service) { return asStatus(service.listItems(TeamId{1})); }},
        {"match", [](CalendarService& service) { return asStatus(service.match(MatchId{1})); }},
        {"session",
         [](CalendarService& service) { return asStatus(service.session(TrainingSessionId{1})); }},
        {"listOpponents",
         [](CalendarService& service) { return asStatus(service.listOpponents()); }},
        {"listCompetitions",
         [](CalendarService& service) { return asStatus(service.listCompetitions(TeamId{1})); }},
        {"attendanceCount",
         [](CalendarService& service) {
             return asStatus(service.attendanceCount(TrainingSessionId{1}));
         }},
    };
}

} // namespace

TEST_F(CalendarServiceTest, OnlyCoachAndAssistantMayEdit) {
    for (const Role role : {Role::Physio, Role::Analyst}) {
        SCOPED_TRACE(toDbText(role));
        logInAs(role);
        ASSERT_FALSE(isAllowed(role, Permission::EditCalendar));
        for (const GuardedCall& call : everyEditCall()) {
            SCOPED_TRACE(call.name);
            expectError(call.run(*m_calendar), ErrorCode::PermissionDenied);
        }
    }
    EXPECT_EQ(count("SELECT COUNT(*) FROM opponent"), 1);
    EXPECT_EQ(count("SELECT COUNT(*) FROM match"), 1);
    EXPECT_EQ(count("SELECT COUNT(*) FROM training_session"), 1);
}

TEST_F(CalendarServiceTest, ACoachSucceedsInEveryEdit) {
    logInAs(Role::Coach);
    // In order: each call leaves what the next one needs.
    for (const GuardedCall& call : everyEditCall()) {
        const Status status = call.run(*m_calendar);
        EXPECT_TRUE(status.has_value()) << call.name << ": " << status.error().detail;
    }
}

TEST_F(CalendarServiceTest, AnAssistantSucceedsInEveryEdit) {
    logInAs(Role::Assistant);
    for (const GuardedCall& call : everyEditCall()) {
        const Status status = call.run(*m_calendar);
        EXPECT_TRUE(status.has_value()) << call.name << ": " << status.error().detail;
    }
}

TEST_F(CalendarServiceTest, NothingWorksWithoutALogin) {
    for (const GuardedCall& call : everyEditCall()) {
        SCOPED_TRACE(call.name);
        expectError(call.run(*m_calendar), ErrorCode::PermissionDenied);
    }
    for (const GuardedCall& call : everyReadCall()) {
        SCOPED_TRACE(call.name);
        expectError(call.run(*m_calendar), ErrorCode::PermissionDenied);
    }
}

TEST_F(CalendarServiceTest, EveryLoggedInRoleReads) {
    for (const Role role : allValues<Role>()) {
        SCOPED_TRACE(toDbText(role));
        logInAs(role);
        for (const GuardedCall& call : everyReadCall()) {
            SCOPED_TRACE(call.name);
            EXPECT_TRUE(call.run(*m_calendar).has_value());
        }
    }
}

TEST_F(CalendarServiceTest, ListItemsGivesMatchesWithTheOpponentNameAndSessions) {
    logInAs(Role::Physio);
    const auto items = m_calendar->listItems(kTeam);
    ASSERT_TRUE(items.has_value()) << items.error().detail;
    ASSERT_EQ(items->matches.size(), 1U);
    EXPECT_EQ(items->matches[0].match.id, kMatch);
    EXPECT_EQ(items->matches[0].opponentName, "Rywal");
    ASSERT_EQ(items->sessions.size(), 1U);
    EXPECT_EQ(items->sessions[0].id, kSession);
}

TEST_F(CalendarServiceTest, ListItemsIsLimitedToTheTeam) {
    logInAs(Role::Coach);
    const auto items = m_calendar->listItems(kOtherTeam);
    ASSERT_TRUE(items.has_value());
    EXPECT_TRUE(items->matches.empty());
    EXPECT_TRUE(items->sessions.empty());
}

TEST_F(CalendarServiceTest, OpponentNameIsUniqueIgnoringCaseAlsoForPolishLetters) {
    logInAs(Role::Coach);
    ASSERT_TRUE(m_calendar->addOpponent(opponentDraft("Łódź")).has_value());
    expectError(m_calendar->addOpponent(opponentDraft("ŁÓDŹ")), ErrorCode::Conflict,
                "opponent_name");
    expectError(m_calendar->addOpponent(opponentDraft("  łódź ")), ErrorCode::Conflict,
                "opponent_name");
    EXPECT_TRUE(m_calendar->addOpponent(opponentDraft("Lodz")).has_value());
    EXPECT_EQ(count("SELECT COUNT(*) FROM opponent"), 3);
}

TEST_F(CalendarServiceTest, OpponentNameErrorsAreOnTheirOwnField) {
    logInAs(Role::Coach);
    expectError(m_calendar->addOpponent(opponentDraft("   ")), ErrorCode::InvalidInput,
                "opponent_name");
    expectError(m_calendar->addOpponent(opponentDraft(std::string(101, 'x').c_str())),
                ErrorCode::InvalidInput, "opponent_name");
    EXPECT_TRUE(m_calendar->addOpponent(opponentDraft(std::string(100, 'x').c_str())).has_value());
}

TEST_F(CalendarServiceTest, RenamingAnOpponentKeepsTheNotesAndTheUniqueness) {
    logInAs(Role::Coach);
    OpponentDraft draft = opponentDraft("Wisla");
    draft.notes = "gra długą piłką";
    const auto wisla = m_calendar->addOpponent(draft);
    ASSERT_TRUE(wisla.has_value());
    ASSERT_TRUE(m_calendar->addOpponent(opponentDraft("Łódź")).has_value());

    ASSERT_TRUE(m_calendar->renameOpponent(*wisla, "  Wisła ").has_value());
    const auto renamed = m_opponents->get(*wisla);
    ASSERT_TRUE(renamed.has_value());
    EXPECT_EQ(renamed->name, "Wisła");
    EXPECT_EQ(renamed->notes, "gra długą piłką");

    expectError(m_calendar->renameOpponent(*wisla, "ŁÓDŹ"), ErrorCode::Conflict, "opponent_name");
    expectError(m_calendar->renameOpponent(*wisla, "  "), ErrorCode::InvalidInput, "opponent_name");
    expectError(m_calendar->renameOpponent(OpponentId{999}, "Nowa"), ErrorCode::NotFound);
    // The same name in another case is the same opponent: allowed.
    EXPECT_TRUE(m_calendar->renameOpponent(*wisla, "WISŁA").has_value());
}

TEST_F(CalendarServiceTest, RenamingAnOpponentNeedsEditCalendar) {
    logInAs(Role::Coach);
    const auto opponent = m_calendar->addOpponent(opponentDraft("Rywal z Ligi"));
    ASSERT_TRUE(opponent.has_value());
    for (const Role role : allValues<Role>()) {
        SCOPED_TRACE(toDbText(role));
        logInAs(role);
        const auto status = m_calendar->renameOpponent(*opponent, "Rywal z Ligi");
        if (isAllowed(role, Permission::EditCalendar)) {
            EXPECT_TRUE(status.has_value());
        } else {
            expectError(status, ErrorCode::PermissionDenied);
        }
        m_session.clear();
    }
}

TEST_F(CalendarServiceTest, MatchNeedsAnOpponent) {
    logInAs(Role::Coach);
    MatchDraft draft = matchDraft();
    draft.opponentId = OpponentId{};
    expectError(m_calendar->addMatch(kTeam, draft), ErrorCode::InvalidInput, "opponent_id");
    expectError(m_calendar->updateMatch(kMatch, draft), ErrorCode::InvalidInput, "opponent_id");
}

TEST_F(CalendarServiceTest, MatchDraftIsValidated) {
    logInAs(Role::Coach);
    MatchDraft draft = matchDraft();
    draft.kickoffTime = std::chrono::hours{24};
    expectError(m_calendar->addMatch(kTeam, draft), ErrorCode::InvalidInput, "kickoff_time");
    draft = matchDraft();
    draft.date = Date{};
    expectError(m_calendar->updateMatch(kMatch, draft), ErrorCode::InvalidInput, "date");
}

TEST_F(CalendarServiceTest, AddedMatchIsPlannedAndKeepsItsFields) {
    logInAs(Role::Assistant);
    const auto id = m_calendar->addMatch(kTeam, matchDraft());
    ASSERT_TRUE(id.has_value()) << id.error().detail;
    const auto match = m_calendar->match(*id);
    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(static_cast<const MatchDraft&>(*match), matchDraft());
    EXPECT_EQ(match->status, MatchStatus::Planned);
    EXPECT_EQ(match->teamId, kTeam);
}

TEST_F(CalendarServiceTest, AMatchThatIsNotPlannedCannotChange) {
    logInAs(Role::Coach);
    ASSERT_TRUE(m_matches->setResult(kMatch, 2, 1, MatchStatus::Finished).has_value());
    expectError(m_calendar->updateMatch(kMatch, matchDraft()), ErrorCode::Conflict,
                "match_not_planned");
    expectError(m_calendar->deleteMatch(kMatch), ErrorCode::Conflict, "match_not_planned");
    EXPECT_EQ(count("SELECT COUNT(*) FROM match"), 1);
}

TEST_F(CalendarServiceTest, DurationBoundsAreChecked) {
    logInAs(Role::Coach);
    for (const int duration : {kMinSessionDuration - 1, kMaxSessionDuration + 1}) {
        SCOPED_TRACE(duration);
        expectError(m_calendar->addSession(kTeam, sessionDraft(duration)), ErrorCode::InvalidInput,
                    "duration_min");
        expectError(m_calendar->updateSession(kSession, sessionDraft(duration)),
                    ErrorCode::InvalidInput, "duration_min");
    }
    for (const int duration : {kMinSessionDuration, kMaxSessionDuration}) {
        SCOPED_TRACE(duration);
        EXPECT_TRUE(m_calendar->addSession(kTeam, sessionDraft(duration)).has_value());
        EXPECT_TRUE(m_calendar->updateSession(kSession, sessionDraft(duration)).has_value());
    }
}

TEST_F(CalendarServiceTest, DeletingASessionTakesItsAttendanceAndKeepsACopy) {
    seedAttendance();
    logInAs(Role::Coach);
    const auto deleted = m_calendar->deleteSession(kSession);
    ASSERT_TRUE(deleted.has_value()) << deleted.error().detail;
    EXPECT_EQ(count("SELECT COUNT(*) FROM training_session"), 0);
    EXPECT_EQ(count("SELECT COUNT(*) FROM attendance"), 0);
    EXPECT_EQ(deleted->session.id, kSession);
    ASSERT_EQ(deleted->attendance.size(), 2U);
    EXPECT_EQ(deleted->attendance[0].rpe, 7);
}

TEST_F(CalendarServiceTest, AttendanceCountSaysHowManyPlayersHaveARow) {
    logInAs(Role::Analyst);
    EXPECT_EQ(m_calendar->attendanceCount(kSession), 0);
    seedAttendance();
    EXPECT_EQ(m_calendar->attendanceCount(kSession), 2);
}

TEST_F(CalendarServiceTest, RestoredSessionHasItsFieldsAndAttendanceUnderANewId) {
    seedAttendance();
    const auto before = m_training->listAttendance(kSession);
    ASSERT_TRUE(before.has_value());
    logInAs(Role::Coach);
    ASSERT_TRUE(m_calendar->updateSession(kSession, sessionDraft()).has_value());

    const auto deleted = m_calendar->deleteSession(kSession);
    ASSERT_TRUE(deleted.has_value());
    const auto id = m_calendar->restoreSession(*deleted);
    ASSERT_TRUE(id.has_value()) << id.error().detail;

    const auto session = m_calendar->session(*id);
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(static_cast<const TrainingSessionDraft&>(*session), sessionDraft());
    EXPECT_EQ(session->teamId, kTeam);
    const auto after = m_training->listAttendance(*id);
    ASSERT_TRUE(after.has_value());
    ASSERT_EQ(after->size(), before->size());
    for (std::size_t i = 0; i < after->size(); ++i) {
        EXPECT_EQ(static_cast<const AttendanceDraft&>((*after)[i]),
                  static_cast<const AttendanceDraft&>((*before)[i]));
    }
}

TEST_F(CalendarServiceTest, SessionRestoreIsAtomic) {
    logInAs(Role::Coach);
    auto deleted = m_calendar->deleteSession(kSession);
    ASSERT_TRUE(deleted.has_value());
    AttendanceDraft row;
    row.playerId = kNoPlayer;
    deleted->attendance.push_back(row);
    EXPECT_FALSE(m_calendar->restoreSession(*deleted).has_value());
    EXPECT_EQ(count("SELECT COUNT(*) FROM training_session"), 0);
}

TEST_F(CalendarServiceTest, ASessionWithMetricValuesStaysWithItsAttendance) {
    seedAttendance();
    ASSERT_TRUE(sql("INSERT INTO metric_definition (id, key, name_pl, name_en, value_type, "
                    "aggregation, scope) VALUES (1, 'm', 'M', 'M', 'integer', 'sum', 'training')"));
    ASSERT_TRUE(sql("INSERT INTO metric_value (metric_id, player_id, session_id, date, value) "
                    "VALUES (1, 1, 1, '2026-11-02', 5)"));
    logInAs(Role::Coach);
    expectError(m_calendar->deleteSession(kSession), ErrorCode::Conflict, "session_in_use");
    EXPECT_EQ(count("SELECT COUNT(*) FROM training_session"), 1);
    EXPECT_EQ(count("SELECT COUNT(*) FROM attendance"), 2);
    // The refused delete leaves the connection usable.
    EXPECT_TRUE(m_calendar->addSession(kTeam, sessionDraft()).has_value());
}

TEST_F(CalendarServiceTest, AMatchWithMetricValuesIsInUse) {
    ASSERT_TRUE(sql("INSERT INTO metric_definition (id, key, name_pl, name_en, value_type, "
                    "aggregation, scope) VALUES (1, 'm', 'M', 'M', 'integer', 'sum', 'match')"));
    ASSERT_TRUE(sql("INSERT INTO metric_value (metric_id, player_id, match_id, date, value) "
                    "VALUES (1, 1, 1, '2026-11-01', 5)"));
    logInAs(Role::Coach);
    expectError(m_calendar->deleteMatch(kMatch), ErrorCode::Conflict, "match_in_use");
    EXPECT_EQ(count("SELECT COUNT(*) FROM match"), 1);
}

TEST_F(CalendarServiceTest, RestoredMatchHasItsFieldsAndChildRowsUnderANewId) {
    logInAs(Role::Coach);
    ASSERT_TRUE(m_calendar->updateMatch(kMatch, matchDraft()).has_value());
    AppearanceDraft appearance;
    appearance.playerId = kPlayer;
    appearance.started = true;
    const std::vector<AppearanceDraft> appearances{appearance};
    ASSERT_TRUE(m_matches->replaceAppearances(kMatch, appearances).has_value());
    MatchEventDraft event;
    event.minute = 12;
    event.type = MatchEventType::Goal;
    event.playerId = kPlayer;
    ASSERT_TRUE(m_matches->appendEvent(kMatch, event).has_value());

    const auto deleted = m_calendar->deleteMatch(kMatch);
    ASSERT_TRUE(deleted.has_value()) << deleted.error().detail;
    EXPECT_EQ(count("SELECT COUNT(*) FROM match"), 0);
    EXPECT_EQ(count("SELECT COUNT(*) FROM appearance"), 0);
    EXPECT_EQ(count("SELECT COUNT(*) FROM match_event"), 0);

    const auto id = m_calendar->restoreMatch(*deleted);
    ASSERT_TRUE(id.has_value()) << id.error().detail;
    const auto match = m_calendar->match(*id);
    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(static_cast<const MatchDraft&>(*match), matchDraft());
    EXPECT_EQ(match->status, MatchStatus::Planned);
    const auto restoredAppearances = m_matches->listAppearances(*id);
    ASSERT_TRUE(restoredAppearances.has_value());
    ASSERT_EQ(restoredAppearances->size(), 1U);
    EXPECT_EQ(restoredAppearances->front().playerId, kPlayer);
    EXPECT_TRUE(restoredAppearances->front().started);
    const auto restoredEvents = m_matches->listEvents(*id);
    ASSERT_TRUE(restoredEvents.has_value());
    ASSERT_EQ(restoredEvents->size(), 1U);
    EXPECT_EQ(restoredEvents->front().minute, 12);
}

// P12 decision 15: the lineup goes with the match and comes back with the undo.
TEST_F(CalendarServiceTest, DeletingAMatchTakesItsLineupAndUndoBringsItBack) {
    logInAs(Role::Coach);
    Lineup lineup = emptyLineup(*findFormation("4-4-2"));
    ASSERT_EQ(moveSlot(lineup, 8, Position::Lw), LineupEdit::Done); // a custom shape
    lineup.pitchSlots[0].player = kPlayer;
    lineup.bench = {kOtherPlayer};
    ASSERT_TRUE(m_lineups->saveForMatch(kMatch, lineup).has_value());

    const auto deleted = m_calendar->deleteMatch(kMatch);
    ASSERT_TRUE(deleted.has_value()) << deleted.error().detail;
    EXPECT_EQ(deleted->lineup, lineup);
    EXPECT_EQ(count("SELECT COUNT(*) FROM match"), 0);
    EXPECT_EQ(count("SELECT COUNT(*) FROM lineup"), 0);

    const auto id = m_calendar->restoreMatch(*deleted);
    ASSERT_TRUE(id.has_value()) << id.error().detail;
    EXPECT_EQ(m_lineups->findForMatch(*id).value(), lineup);
    EXPECT_EQ(m_calendar->match(*id).value().formation, "4-3-1-2");
    EXPECT_EQ(count("SELECT COUNT(*) FROM lineup"), 1);
}

TEST_F(CalendarServiceTest, DeletingAMatchWithoutALineup) {
    logInAs(Role::Coach);
    const auto deleted = m_calendar->deleteMatch(kMatch);
    ASSERT_TRUE(deleted.has_value());
    EXPECT_EQ(deleted->lineup, std::nullopt);
    const auto id = m_calendar->restoreMatch(*deleted);
    ASSERT_TRUE(id.has_value());
    EXPECT_EQ(count("SELECT COUNT(*) FROM lineup"), 0);
    EXPECT_EQ(m_calendar->match(*id).value().formation, std::nullopt);
}

TEST_F(CalendarServiceTest, MatchRestoreIsAtomic) {
    logInAs(Role::Coach);
    auto deleted = m_calendar->deleteMatch(kMatch);
    ASSERT_TRUE(deleted.has_value());
    AppearanceDraft appearance;
    appearance.playerId = kPlayer;
    deleted->appearances.push_back(appearance);
    // The last step fails, after the match and its appearances were written.
    MatchEventDraft event;
    event.minute = 12;
    event.type = MatchEventType::Goal;
    event.playerId = kNoPlayer;
    deleted->events.push_back(event);

    EXPECT_FALSE(m_calendar->restoreMatch(*deleted).has_value());

    EXPECT_EQ(count("SELECT COUNT(*) FROM match"), 0);
    EXPECT_EQ(count("SELECT COUNT(*) FROM appearance"), 0);
    EXPECT_EQ(count("SELECT COUNT(*) FROM match_event"), 0);
}

TEST_F(CalendarServiceTest, DeletingWhatIsGoneIsNotFound) {
    logInAs(Role::Coach);
    expectError(m_calendar->deleteMatch(MatchId{999}), ErrorCode::NotFound);
    expectError(m_calendar->deleteSession(TrainingSessionId{999}), ErrorCode::NotFound);
}
