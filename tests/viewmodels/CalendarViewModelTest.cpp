#include "viewmodels/CalendarViewModel.hpp"

#include "ViewModelFixture.hpp"
#include "core/domain/Match.hpp"
#include "core/domain/TrainingSession.hpp"
#include "data/repositories/TrainingRepository.hpp"
#include "services/CalendarService.hpp"
#include "services/SquadService.hpp"
#include "viewmodels/IsoDates.hpp"

#include <QVariantList>
#include <QVariantMap>

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

using namespace fdss::core;
using namespace fdss::tests;
using fdss::viewmodels::CalendarListModel;
using fdss::viewmodels::CalendarViewModel;
using fdss::viewmodels::CalendarWeekModel;
using fdss::viewmodels::IsoDates;

namespace {

// FakeClock starts on Sunday 2026-11-01.
constexpr auto kToday = "2026-11-01";

Date dateOf(const char* iso) { return IsoDates::parse(QString::fromUtf8(iso)).value_or(Date{}); }

class CalendarViewModelTest : public ViewModelTest {
protected:
    void SetUp() override {
        ViewModelTest::SetUp();
        if (HasFatalFailure()) {
            return;
        }
        completeSetup();
        m_team = registry().setup().clubContext()->team.id;
        OpponentDraft opponent;
        opponent.name = "Rywal";
        const auto id = registry().calendar().addOpponent(opponent);
        ASSERT_TRUE(id.has_value()) << id.error().detail;
        m_opponent = *id;
    }

    MatchId addMatch(const char* date, std::optional<TimeOfDay> kickoff = std::nullopt) {
        MatchDraft draft;
        draft.opponentId = m_opponent;
        draft.date = dateOf(date);
        draft.kickoffTime = kickoff;
        draft.competition = "Liga";
        const auto id = registry().calendar().addMatch(m_team, draft);
        EXPECT_TRUE(id.has_value()) << id.error().detail;
        return id.value_or(MatchId{});
    }

    TrainingSessionId addSession(const char* date, std::optional<TimeOfDay> start = std::nullopt) {
        TrainingSessionDraft draft;
        draft.date = dateOf(date);
        draft.startTime = start;
        const auto id = registry().calendar().addSession(m_team, draft);
        EXPECT_TRUE(id.has_value()) << id.error().detail;
        return id.value_or(TrainingSessionId{});
    }

    PlayerId addPlayer(const char* lastName) {
        PlayerDraft draft;
        draft.firstName = "Jan";
        draft.lastName = lastName;
        const auto id = registry().squad().addPlayer(m_team, draft);
        EXPECT_TRUE(id.has_value()) << id.error().detail;
        return id.value_or(PlayerId{});
    }

    // "date kind" of every row, in the order of the list.
    static std::vector<std::string> rows(const CalendarViewModel& calendar) {
        std::vector<std::string> result;
        for (int row = 0; row < calendar.items()->rowCount(); ++row) {
            const QVariantMap values = calendar.rowAt(row);
            result.push_back((values.value(QStringLiteral("date")).toString() + u' ' +
                              values.value(QStringLiteral("time")).toString() + u' ' +
                              values.value(QStringLiteral("kind")).toString())
                                 .toStdString());
        }
        return result;
    }

    static QVariantList itemsOn(const CalendarViewModel& calendar, int day) {
        return calendar.week()->index(day).data(CalendarWeekModel::ItemsRole).toList();
    }

    TeamId m_team;
    OpponentId m_opponent;
};

TEST_F(CalendarViewModelTest, ListHasUpcomingFromTheNearestThenPastFromTheMostRecent) {
    addMatch("2026-10-01");
    addSession("2026-11-20");
    addSession("2026-10-30");
    addMatch("2026-11-05");
    addSession(kToday);
    CalendarViewModel calendar;
    calendar.setTeamId(m_team.value);

    const std::vector<std::string> expected{"2026-11-01  session", "2026-11-05  match",
                                            "2026-11-20  session", "2026-10-30  session",
                                            "2026-10-01  match"};
    EXPECT_EQ(rows(calendar), expected);
}

TEST_F(CalendarViewModelTest, ListSectionsSplitOnToday) {
    addSession(kToday);
    addSession("2026-10-31");
    CalendarViewModel calendar;
    calendar.setTeamId(m_team.value);

    EXPECT_EQ(calendar.rowAt(0).value(QStringLiteral("section")).toString(),
              QStringLiteral("Upcoming"));
    EXPECT_EQ(calendar.rowAt(1).value(QStringLiteral("section")).toString(),
              QStringLiteral("Past"));
}

TEST_F(CalendarViewModelTest, WithinADayItemsGoByTimeThoseWithoutATimeLast) {
    using std::chrono::hours;
    addSession("2026-11-03");
    addSession("2026-11-03", hours{18});
    addMatch("2026-11-03", hours{18});
    addMatch("2026-11-03", hours{10});
    // A past day keeps the same order inside the day.
    addSession("2026-10-20", hours{18});
    addSession("2026-10-20", hours{9});
    CalendarViewModel calendar;
    calendar.setTeamId(m_team.value);

    const std::vector<std::string> expected{"2026-11-03 10:00 match",   "2026-11-03 18:00 match",
                                            "2026-11-03 18:00 session", "2026-11-03  session",
                                            "2026-10-20 09:00 session", "2026-10-20 18:00 session"};
    EXPECT_EQ(rows(calendar), expected);
}

TEST_F(CalendarViewModelTest, RowsCarryWhatTheListShows) {
    addMatch("2026-11-07", std::chrono::hours{15} + std::chrono::minutes{30});
    CalendarViewModel calendar;
    calendar.setTeamId(m_team.value);

    const QVariantMap row = calendar.rowAt(0);
    EXPECT_EQ(row.value(QStringLiteral("kind")).toString(), QStringLiteral("match"));
    EXPECT_EQ(row.value(QStringLiteral("title")).toString(), QStringLiteral("Rywal"));
    EXPECT_EQ(row.value(QStringLiteral("time")).toString(), QStringLiteral("15:30"));
    EXPECT_EQ(row.value(QStringLiteral("details")).toString(), QStringLiteral("Home · Liga"));
    EXPECT_EQ(row.value(QStringLiteral("statusKey")).toString(), QStringLiteral("planned"));
    EXPECT_EQ(row.value(QStringLiteral("statusText")).toString(), QStringLiteral("Planned"));
    EXPECT_EQ(row.value(QStringLiteral("dateText")).toString(),
              QStringLiteral("Saturday, 07/11/2026"));
    EXPECT_TRUE(calendar.rowAt(1).isEmpty());
    EXPECT_TRUE(calendar.rowAt(-1).isEmpty());
}

TEST_F(CalendarViewModelTest, RowOfFindsAnItemByItsKindAndId) {
    const TrainingSessionId session = addSession("2026-11-02");
    const MatchId match = addMatch("2026-11-03");
    CalendarViewModel calendar;
    calendar.setTeamId(m_team.value);

    EXPECT_EQ(calendar.rowOf(QStringLiteral("session"), session.value), 0);
    EXPECT_EQ(calendar.rowOf(QStringLiteral("match"), match.value), 1);
    EXPECT_EQ(calendar.rowOf(QStringLiteral("match"), 999), -1);
}

TEST_F(CalendarViewModelTest, ASundayBelongsToTheWeekOfThePrecedingMonday) {
    CalendarViewModel calendar;

    EXPECT_EQ(calendar.weekStart(), QStringLiteral("2026-10-26"));
    EXPECT_EQ(calendar.weekEnd(), QStringLiteral("2026-11-01"));
    EXPECT_TRUE(calendar.showsCurrentWeek());
}

TEST_F(CalendarViewModelTest, AMondayStartsItsOwnWeek) {
    CalendarViewModel calendar;
    calendar.showWeekOf(QStringLiteral("2026-11-02"));

    EXPECT_EQ(calendar.weekStart(), QStringLiteral("2026-11-02"));
    EXPECT_EQ(calendar.weekEnd(), QStringLiteral("2026-11-08"));
    EXPECT_FALSE(calendar.showsCurrentWeek());
}

TEST_F(CalendarViewModelTest, AWeekCanSpanTheChangeOfTheYear) {
    CalendarViewModel calendar;
    // A Friday.
    calendar.showWeekOf(QStringLiteral("2027-01-01"));

    EXPECT_EQ(calendar.weekStart(), QStringLiteral("2026-12-28"));
    EXPECT_EQ(calendar.weekEnd(), QStringLiteral("2027-01-03"));
    EXPECT_EQ(calendar.week()->index(0).data(CalendarWeekModel::DateRole).toString(),
              QStringLiteral("2026-12-28"));
    EXPECT_EQ(calendar.week()->index(6).data(CalendarWeekModel::DateRole).toString(),
              QStringLiteral("2027-01-03"));
    calendar.nextWeek();
    EXPECT_EQ(calendar.weekStart(), QStringLiteral("2027-01-04"));
    calendar.previousWeek();
    calendar.previousWeek();
    EXPECT_EQ(calendar.weekStart(), QStringLiteral("2026-12-21"));
}

TEST_F(CalendarViewModelTest, TheWeekHasSevenDaysNamedFromMonday) {
    CalendarViewModel calendar;

    ASSERT_EQ(calendar.week()->rowCount(), CalendarWeekModel::kDaysInWeek);
    EXPECT_EQ(calendar.week()->index(0).data(CalendarWeekModel::DayNameRole).toString(),
              QStringLiteral("Monday"));
    EXPECT_EQ(calendar.week()->index(6).data(CalendarWeekModel::DayNameRole).toString(),
              QStringLiteral("Sunday"));
    EXPECT_FALSE(calendar.week()->index(5).data(CalendarWeekModel::TodayRole).toBool());
    EXPECT_TRUE(calendar.week()->index(6).data(CalendarWeekModel::TodayRole).toBool());
}

TEST_F(CalendarViewModelTest, ItemsAppearOnTheirDayOfTheWeekOnly) {
    using std::chrono::hours;
    // The Monday and the Sunday of the week of 2026-11-02, and the days just around it.
    addSession("2026-11-01");
    addMatch("2026-11-02", hours{18});
    addSession("2026-11-02", hours{9});
    addSession("2026-11-08");
    addMatch("2026-11-09");
    CalendarViewModel calendar;
    calendar.setTeamId(m_team.value);
    calendar.showWeekOf(QStringLiteral("2026-11-04"));

    const QVariantList monday = itemsOn(calendar, 0);
    ASSERT_EQ(monday.size(), 2);
    EXPECT_EQ(monday[0].toMap().value(QStringLiteral("kind")).toString(),
              QStringLiteral("session"));
    EXPECT_EQ(monday[1].toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Rywal"));
    for (int day = 1; day < 6; ++day) {
        EXPECT_TRUE(itemsOn(calendar, day).isEmpty()) << day;
    }
    EXPECT_EQ(itemsOn(calendar, 6).size(), 1);
}

TEST_F(CalendarViewModelTest, GoToTodayComesBackToTheCurrentWeek) {
    CalendarViewModel calendar;
    calendar.showWeekOf(QStringLiteral("2027-03-10"));
    ASSERT_FALSE(calendar.showsCurrentWeek());
    SignalCount changed(&calendar, &CalendarViewModel::weekChanged);

    calendar.goToToday();

    EXPECT_TRUE(calendar.showsCurrentWeek());
    EXPECT_EQ(calendar.weekStart(), QStringLiteral("2026-10-26"));
    EXPECT_EQ(changed.count(), 1);
}

TEST_F(CalendarViewModelTest, NotADateChangesNothing) {
    CalendarViewModel calendar;
    calendar.showWeekOf(QStringLiteral("2026-13-40"));
    EXPECT_EQ(calendar.weekStart(), QStringLiteral("2026-10-26"));
}

TEST_F(CalendarViewModelTest, TheWeeksEndAtTheSupportedDates) {
    CalendarViewModel calendar;
    // 1900-01-01 was a Monday.
    calendar.showWeekOf(QStringLiteral("1900-01-03"));
    calendar.previousWeek();
    EXPECT_EQ(calendar.weekStart(), QStringLiteral("1900-01-01"));
    calendar.showWeekOf(QStringLiteral("2100-12-31"));
    calendar.nextWeek();
    EXPECT_EQ(calendar.weekStart(), QStringLiteral("2100-12-27"));
}

TEST_F(CalendarViewModelTest, ANewItemStartsTodayOrOnTheShownMonday) {
    CalendarViewModel calendar;
    EXPECT_EQ(calendar.defaultNewItemDate(), QString::fromUtf8(kToday));
    calendar.nextWeek();
    EXPECT_EQ(calendar.defaultNewItemDate(), QStringLiteral("2026-11-02"));
}

TEST_F(CalendarViewModelTest, AViewOnTheCurrentWeekFollowsItOverMidnight) {
    CalendarViewModel calendar;
    m_clock->advance(std::chrono::days{1});
    calendar.reload();
    EXPECT_EQ(calendar.weekStart(), QStringLiteral("2026-11-02"));
    EXPECT_TRUE(calendar.showsCurrentWeek());
}

TEST_F(CalendarViewModelTest, AViewOnAnotherWeekStaysThereOverMidnight) {
    CalendarViewModel calendar;
    calendar.showWeekOf(QStringLiteral("2027-03-10"));
    m_clock->advance(std::chrono::days{1});
    calendar.reload();
    EXPECT_EQ(calendar.weekStart(), QStringLiteral("2027-03-08"));
}

TEST_F(CalendarViewModelTest, AReadErrorIsShownAndTryingAgainClearsIt) {
    addSession(kToday);
    CalendarViewModel calendar;
    calendar.setTeamId(m_team.value);
    ASSERT_TRUE(calendar.loadError().isEmpty());
    ASSERT_EQ(calendar.items()->rowCount(), 1);

    ASSERT_TRUE(
        m_db->execSql(QStringLiteral("ALTER TABLE training_session RENAME TO session_gone"), "test")
            .has_value());
    SignalCount changed(&calendar, &CalendarViewModel::loadErrorChanged);
    calendar.reload();
    EXPECT_FALSE(calendar.loadError().isEmpty());
    EXPECT_EQ(calendar.items()->rowCount(), 0);
    EXPECT_GE(changed.count(), 1);

    ASSERT_TRUE(
        m_db->execSql(QStringLiteral("ALTER TABLE session_gone RENAME TO training_session"), "test")
            .has_value());
    calendar.reload();
    EXPECT_TRUE(calendar.loadError().isEmpty());
    EXPECT_EQ(calendar.items()->rowCount(), 1);
}

TEST_F(CalendarViewModelTest, EmptyMeansNothingInAnyWeek) {
    CalendarViewModel calendar;
    calendar.setTeamId(m_team.value);
    EXPECT_TRUE(calendar.empty());
    SignalCount changed(&calendar, &CalendarViewModel::emptyChanged);

    // Far from the shown week, so that week alone has nothing to show.
    const TrainingSessionId session = addSession("2027-06-01");
    calendar.reload();
    EXPECT_FALSE(calendar.empty());
    EXPECT_GE(changed.count(), 1);

    ASSERT_TRUE(calendar.deleteSession(session.value));
    EXPECT_TRUE(calendar.empty());
}

TEST_F(CalendarViewModelTest, WithoutATeamTheCalendarIsEmpty) {
    addSession(kToday);
    CalendarViewModel calendar;
    calendar.reload();
    EXPECT_EQ(calendar.items()->rowCount(), 0);
    EXPECT_TRUE(calendar.loadError().isEmpty());
}

TEST_F(CalendarViewModelTest, DeletingASessionWithoutAttendanceCountsZero) {
    const TrainingSessionId session = addSession(kToday);
    CalendarViewModel calendar;
    calendar.setTeamId(m_team.value);

    EXPECT_EQ(calendar.attendanceCount(session.value), 0);
    ASSERT_TRUE(calendar.deleteSession(session.value));
    EXPECT_EQ(calendar.items()->rowCount(), 0);
}

TEST_F(CalendarViewModelTest, AttendanceCountAndTheQuestionNameThePlayers) {
    const TrainingSessionId session = addSession(kToday);
    AttendanceDraft first;
    first.playerId = addPlayer("Kowalski");
    first.rpe = 6;
    AttendanceDraft second;
    second.playerId = addPlayer("Nowak");
    second.status = AttendanceStatus::Absent;
    const std::vector<AttendanceDraft> rows{first, second};
    ASSERT_TRUE(fdss::data::TrainingRepository(*m_db).saveAttendance(session, rows).has_value());
    CalendarViewModel calendar;
    calendar.setTeamId(m_team.value);

    const int count = calendar.attendanceCount(session.value);
    EXPECT_EQ(count, 2);
    EXPECT_TRUE(calendar.deleteConfirmationText(count).contains(QStringLiteral("2 players")));
    EXPECT_TRUE(calendar.deleteConfirmationText(1).contains(QStringLiteral("1 player.")));
}

TEST_F(CalendarViewModelTest, UndoBringsTheSessionBackWithItsAttendance) {
    const TrainingSessionId session = addSession(kToday);
    AttendanceDraft row;
    row.playerId = addPlayer("Kowalski");
    row.rpe = 6;
    const std::vector<AttendanceDraft> rows{row};
    ASSERT_TRUE(fdss::data::TrainingRepository(*m_db).saveAttendance(session, rows).has_value());
    CalendarViewModel calendar;
    calendar.setTeamId(m_team.value);

    ASSERT_TRUE(calendar.deleteSession(session.value));
    ASSERT_EQ(calendar.items()->rowCount(), 0);
    ASSERT_TRUE(calendar.undoDelete());

    ASSERT_EQ(calendar.items()->rowCount(), 1);
    const qint64 restored = calendar.rowAt(0).value(QStringLiteral("itemId")).toLongLong();
    EXPECT_EQ(calendar.attendanceCount(restored), 1);
}

TEST_F(CalendarViewModelTest, UndoBringsTheMatchBack) {
    const MatchId match = addMatch(kToday);
    CalendarViewModel calendar;
    calendar.setTeamId(m_team.value);

    ASSERT_TRUE(calendar.deleteMatch(match.value));
    ASSERT_EQ(calendar.items()->rowCount(), 0);
    ASSERT_TRUE(calendar.undoDelete());
    ASSERT_EQ(calendar.items()->rowCount(), 1);
    EXPECT_EQ(calendar.rowAt(0).value(QStringLiteral("title")).toString(), QStringLiteral("Rywal"));
}

TEST_F(CalendarViewModelTest, UndoWorksOnceAndOnlyAfterADelete) {
    const MatchId match = addMatch(kToday);
    CalendarViewModel calendar;
    calendar.setTeamId(m_team.value);
    EXPECT_FALSE(calendar.undoDelete());

    ASSERT_TRUE(calendar.deleteMatch(match.value));
    EXPECT_TRUE(calendar.undoDelete());
    EXPECT_FALSE(calendar.undoDelete());
    EXPECT_EQ(calendar.items()->rowCount(), 1);
}

TEST_F(CalendarViewModelTest, UndoBelongsToTheLastDelete) {
    const MatchId match = addMatch(kToday);
    const TrainingSessionId session = addSession(kToday);
    CalendarViewModel calendar;
    calendar.setTeamId(m_team.value);

    ASSERT_TRUE(calendar.deleteMatch(match.value));
    ASSERT_TRUE(calendar.deleteSession(session.value));
    ASSERT_TRUE(calendar.undoDelete());

    ASSERT_EQ(calendar.items()->rowCount(), 1);
    EXPECT_EQ(calendar.rowAt(0).value(QStringLiteral("kind")).toString(),
              QStringLiteral("session"));
}

TEST_F(CalendarViewModelTest, ARoleWithoutEditCalendarIsRefusedAndSeesTheCalendar) {
    const MatchId match = addMatch(kToday);
    const StaffUserId physio = addStaff("Fizjo", Role::Physio);
    const auto login = registry().auth().login(physio, kPin);
    ASSERT_TRUE(login.has_value());
    CalendarViewModel calendar;
    calendar.setTeamId(m_team.value);
    SignalCount errors(&calendar, &CalendarViewModel::errorOccurred);

    EXPECT_EQ(calendar.items()->rowCount(), 1);
    EXPECT_FALSE(calendar.deleteMatch(match.value));
    EXPECT_EQ(errors.count(), 1);
    EXPECT_EQ(calendar.items()->rowCount(), 1);
}

} // namespace
