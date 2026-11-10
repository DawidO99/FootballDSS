#include "viewmodels/TrainingSessionViewModel.hpp"

#include "ViewModelFixture.hpp"
#include "core/domain/Availability.hpp"
#include "core/domain/Player.hpp"
#include "core/domain/TrainingSession.hpp"
#include "services/AuthService.hpp"
#include "services/CalendarService.hpp"
#include "services/Session.hpp"
#include "services/SquadService.hpp"
#include "services/TrainingService.hpp"
#include "viewmodels/ErrorPresenter.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace fdss::core;
using namespace fdss::tests;
using fdss::viewmodels::SessionPickerModel;
using fdss::viewmodels::TrainingSessionViewModel;

namespace {

using VM = TrainingSessionViewModel;

// FakeClock says today is 2026-11-01.
constexpr Date kToday{std::chrono::year{2026}, std::chrono::November, std::chrono::day{1}};
constexpr Date kYesterday{std::chrono::year{2026}, std::chrono::October, std::chrono::day{31}};
constexpr Date kLastWeek{std::chrono::year{2026}, std::chrono::October, std::chrono::day{25}};
constexpr Date kTomorrow{std::chrono::year{2026}, std::chrono::November, std::chrono::day{2}};
constexpr Date kNextWeek{std::chrono::year{2026}, std::chrono::November, std::chrono::day{8}};
constexpr int kDuration = 90;

class TrainingSessionViewModelTest : public ViewModelTest {
protected:
    void SetUp() override {
        ViewModelTest::SetUp();
        if (HasFatalFailure()) {
            return;
        }
        completeSetup();
        m_team = registry().setup().clubContext()->team.id;
    }

    TrainingSessionId addSession(Date date, int duration = kDuration) {
        TrainingSessionDraft draft;
        draft.date = date;
        draft.durationMin = duration;
        const auto id = registry().calendar().addSession(m_team, draft);
        EXPECT_TRUE(id.has_value()) << id.error().detail;
        return id.value_or(TrainingSessionId{});
    }

    PlayerId addPlayer(const char* first, const char* last, std::optional<int> number) {
        PlayerDraft draft;
        draft.firstName = first;
        draft.lastName = last;
        draft.shirtNumber = number;
        const auto id = registry().squad().addPlayer(m_team, draft);
        EXPECT_TRUE(id.has_value()) << id.error().detail;
        return id.value_or(PlayerId{});
    }

    void addAvailability(PlayerId player, AvailabilityType type, Date from,
                         std::optional<Date> expectedTo = {}) {
        AvailabilityDraft draft;
        draft.type = type;
        draft.fromDate = from;
        draft.expectedTo = expectedTo;
        const auto id = registry().squad().addAvailability(player, draft);
        ASSERT_TRUE(id.has_value()) << id.error().detail;
    }

    void logInAs(Role role) {
        const StaffUserId id = addStaff("Inny", role);
        const auto check = registry().auth().login(id, kPin);
        ASSERT_TRUE(check.has_value());
        ASSERT_EQ(check->status, fdss::services::PinCheckStatus::Accepted);
    }

    // One session today and two players, numbers 7 and 9: rows 0 and 1.
    void seedSheet() {
        m_session = addSession(kToday);
        m_first = addPlayer("Jan", "Kowalski", 7);
        m_second = addPlayer("Adam", "Nowak", 9);
    }

    std::unique_ptr<VM> makeSheet() {
        auto sheet = std::make_unique<VM>();
        sheet->setTeamId(m_team.value);
        return sheet;
    }

    static QVariant value(const VM& sheet, int row, VM::DataRole role) {
        return sheet.index(row).data(role);
    }

    static QString status(const VM& sheet, int row) {
        return value(sheet, row, VM::StatusRole).toString();
    }

    static QVariant rpe(const VM& sheet, int row) { return value(sheet, row, VM::RpeRole); }

    TeamId m_team;
    TrainingSessionId m_session;
    PlayerId m_first;
    PlayerId m_second;
};

// A delegate reads the model while the signal is emitted: a reset creates it right then, so what it
// reads must already be the new session. Returns the minutes of the first row at every signal.
class MinutesWatcher {
public:
    explicit MinutesWatcher(const VM& sheet) : m_sheet(sheet) {
        const auto record = [this] {
            if (m_sheet.rowCount() > 0) {
                m_seen.push_back(m_sheet.index(0).data(VM::MinutesRole).toInt());
                m_durations.push_back(m_sheet.durationMin());
            }
        };
        QObject::connect(&sheet, &QAbstractItemModel::modelReset, &m_context, record);
        QObject::connect(&sheet, &QAbstractItemModel::dataChanged, &m_context, record);
    }

    void expectOnlyEver(int minutes) const {
        ASSERT_FALSE(m_seen.empty());
        for (const int seen : m_seen) {
            EXPECT_EQ(seen, minutes);
        }
        for (const int duration : m_durations) {
            EXPECT_EQ(duration, minutes);
        }
    }

    void clear() {
        m_seen.clear();
        m_durations.clear();
    }

private:
    const VM& m_sheet;
    QObject m_context;
    std::vector<int> m_seen;
    std::vector<int> m_durations;
};

TEST_F(TrainingSessionViewModelTest, FirstLoadShowsTheDurationFromTheStart) {
    seedSheet();
    VM sheet;
    const MinutesWatcher watcher(sheet);
    sheet.setTeamId(m_team.value);
    watcher.expectOnlyEver(kDuration);
}

TEST_F(TrainingSessionViewModelTest, SheetFilledByAReloadShowsTheDurationFromTheStart) {
    // The sidebar way: the view is loaded before the first session exists.
    m_first = addPlayer("Jan", "Kowalski", 7);
    VM sheet;
    sheet.setTeamId(m_team.value);
    ASSERT_EQ(sheet.count(), 0);
    MinutesWatcher watcher(sheet);
    addSession(kToday);
    sheet.reload();
    ASSERT_EQ(sheet.count(), 1);
    watcher.expectOnlyEver(kDuration);
}

TEST_F(TrainingSessionViewModelTest, AnotherSessionShowsItsDurationFromTheStart) {
    seedSheet();
    const TrainingSessionId shorter = addSession(kLastWeek, 45);
    VM sheet;
    sheet.setTeamId(m_team.value);
    MinutesWatcher watcher(sheet);
    sheet.openSession(shorter.value);
    watcher.expectOnlyEver(45);
}

TEST_F(TrainingSessionViewModelTest, NewSquadAndDurationShowFromTheStart) {
    seedSheet();
    VM sheet;
    sheet.setTeamId(m_team.value);
    MinutesWatcher watcher(sheet);
    addPlayer("Ewa", "Zych", 11);
    TrainingSessionDraft draft;
    draft.date = kToday;
    draft.durationMin = 60;
    ASSERT_TRUE(registry().calendar().updateSession(m_session, draft).has_value());
    sheet.reload();
    ASSERT_EQ(sheet.count(), 3);
    watcher.expectOnlyEver(60);
    EXPECT_EQ(sheet.index(2).data(VM::MinutesRole).toInt(), 60);
}

TEST_F(TrainingSessionViewModelTest, WhatTheRowShowsIsWhatASaveWrites) {
    seedSheet();
    VM sheet;
    sheet.setTeamId(m_team.value);
    addPlayer("Ewa", "Zych", 11);
    TrainingSessionDraft draft;
    draft.date = kToday;
    draft.durationMin = 60;
    ASSERT_TRUE(registry().calendar().updateSession(m_session, draft).has_value());
    sheet.reload();
    sheet.enterRpeDigit(2, 5);
    EXPECT_EQ(sheet.index(2).data(VM::LoadRole).toInt(), 5 * 60);
    ASSERT_TRUE(sheet.save());
    const auto stored = registry().training().sheet(m_session);
    ASSERT_TRUE(stored.has_value());
    // No override: the load counts the duration of the session, the same as the row showed.
    EXPECT_FALSE(stored->rows[2].draft.minutesOverride.has_value());
    EXPECT_EQ(stored->session.durationMin, 60);
}

TEST_F(TrainingSessionViewModelTest, NoSessionsMeansAnEmptySheet) {
    addPlayer("Jan", "Kowalski", 7);
    const auto sheet = makeSheet();
    EXPECT_FALSE(sheet->hasSessions());
    EXPECT_EQ(sheet->sessionId(), 0);
    EXPECT_EQ(sheet->count(), 0);
    EXPECT_FALSE(sheet->canSave());
}

TEST_F(TrainingSessionViewModelTest, OpensTheMostRecentSessionOnOrBeforeToday) {
    addSession(kLastWeek);
    const TrainingSessionId yesterday = addSession(kYesterday);
    addSession(kTomorrow);
    const auto sheet = makeSheet();
    EXPECT_EQ(sheet->sessionId(), yesterday.value);
}

TEST_F(TrainingSessionViewModelTest, TodayCountsAsOnOrBefore) {
    addSession(kYesterday);
    const TrainingSessionId today = addSession(kToday);
    const auto sheet = makeSheet();
    EXPECT_EQ(sheet->sessionId(), today.value);
}

TEST_F(TrainingSessionViewModelTest, OnlyFutureSessionsOpenTheNearestOne) {
    addSession(kNextWeek);
    const TrainingSessionId tomorrow = addSession(kTomorrow);
    const auto sheet = makeSheet();
    EXPECT_EQ(sheet->sessionId(), tomorrow.value);
    EXPECT_TRUE(sheet->hasSessions());
}

TEST_F(TrainingSessionViewModelTest, PickerListsNewestFirst) {
    const TrainingSessionId older = addSession(kLastWeek);
    const TrainingSessionId newer = addSession(kTomorrow);
    const auto sheet = makeSheet();
    const SessionPickerModel* picker = sheet->sessions();
    ASSERT_EQ(picker->rowCount(), 2);
    EXPECT_EQ(picker->index(0).data(SessionPickerModel::ValueRole).toLongLong(), newer.value);
    EXPECT_EQ(picker->index(1).data(SessionPickerModel::ValueRole).toLongLong(), older.value);
    EXPECT_FALSE(picker->index(0).data(SessionPickerModel::TextRole).toString().isEmpty());
}

TEST_F(TrainingSessionViewModelTest, MissingSessionReportsAndFallsBack) {
    seedSheet();
    const auto sheet = makeSheet();
    const SignalCount errors(sheet.get(), &VM::errorOccurred);
    sheet->openSession(9999);
    EXPECT_EQ(errors.count(), 1);
    EXPECT_EQ(sheet->sessionId(), m_session.value);
}

TEST_F(TrainingSessionViewModelTest, RowsAreSortedByNumberThenSurname) {
    m_session = addSession(kToday);
    addPlayer("Ola", "Żak", {});
    addPlayer("Iza", "Łoś", {});
    addPlayer("Ewa", "Zych", {});
    addPlayer("Jan", "Kowalski", 20);
    addPlayer("Adam", "Nowak", 3);
    const auto sheet = makeSheet();
    ASSERT_EQ(sheet->count(), 5);
    EXPECT_EQ(value(*sheet, 0, VM::ShirtNumberRole).toInt(), 3);
    EXPECT_EQ(value(*sheet, 1, VM::ShirtNumberRole).toInt(), 20);
    EXPECT_TRUE(value(*sheet, 2, VM::PlayerNameRole).toString().contains(u"Łoś"));
    EXPECT_TRUE(value(*sheet, 3, VM::PlayerNameRole).toString().contains(u"Zych"));
    EXPECT_TRUE(value(*sheet, 4, VM::PlayerNameRole).toString().contains(u"Żak"));
    EXPECT_FALSE(value(*sheet, 4, VM::ShirtNumberRole).isValid());
}

TEST_F(TrainingSessionViewModelTest, ArchivedPlayersAreNotOnTheSheet) {
    seedSheet();
    ASSERT_TRUE(registry().squad().archivePlayer(m_second).has_value());
    const auto sheet = makeSheet();
    EXPECT_EQ(sheet->count(), 1);
}

TEST_F(TrainingSessionViewModelTest, PlayersStartPresentWithDefaultMinutes) {
    seedSheet();
    const auto sheet = makeSheet();
    EXPECT_EQ(sheet->durationMin(), kDuration);
    EXPECT_EQ(status(*sheet, 0), QStringLiteral("present"));
    EXPECT_FALSE(rpe(*sheet, 0).isValid());
    EXPECT_EQ(value(*sheet, 0, VM::MinutesRole).toInt(), kDuration);
    EXPECT_TRUE(value(*sheet, 0, VM::CellsEnabledRole).toBool());
    EXPECT_FALSE(value(*sheet, 0, VM::LoadRole).isValid());
}

TEST_F(TrainingSessionViewModelTest, InjuredAndIllPlayersStartInjured) {
    seedSheet();
    addAvailability(m_first, AvailabilityType::Injury, kYesterday);
    addAvailability(m_second, AvailabilityType::Illness, kToday);
    const auto sheet = makeSheet();
    EXPECT_EQ(status(*sheet, 0), QStringLiteral("injured"));
    EXPECT_EQ(status(*sheet, 1), QStringLiteral("injured"));
    EXPECT_FALSE(value(*sheet, 0, VM::CellsEnabledRole).toBool());
    EXPECT_FALSE(value(*sheet, 0, VM::MinutesRole).isValid());
    EXPECT_FALSE(sheet->dirty());
}

TEST_F(TrainingSessionViewModelTest, AbsencesStartExcused) {
    seedSheet();
    addAvailability(m_first, AvailabilityType::Absence, kToday, kToday);
    const auto sheet = makeSheet();
    EXPECT_EQ(status(*sheet, 0), QStringLiteral("excused"));
    EXPECT_EQ(status(*sheet, 1), QStringLiteral("present"));
    EXPECT_FALSE(value(*sheet, 0, VM::CellsEnabledRole).toBool());
    EXPECT_FALSE(sheet->dirty());
}

TEST_F(TrainingSessionViewModelTest, SuspensionsStartPresent) {
    seedSheet();
    addAvailability(m_first, AvailabilityType::Suspension, kToday, kToday);
    const auto sheet = makeSheet();
    EXPECT_EQ(status(*sheet, 0), QStringLiteral("present"));
}

TEST_F(TrainingSessionViewModelTest, WhatTheCoachSavedWinsOverTheDefault) {
    seedSheet();
    addAvailability(m_first, AvailabilityType::Injury, kYesterday);
    addAvailability(m_second, AvailabilityType::Absence, kToday, kToday);
    {
        const auto sheet = makeSheet();
        sheet->setStatus(0, QStringLiteral("present"));
        sheet->setStatus(1, QStringLiteral("absent"));
        ASSERT_TRUE(sheet->save());
    }
    const auto sheet = makeSheet();
    EXPECT_EQ(status(*sheet, 0), QStringLiteral("present"));
    EXPECT_EQ(status(*sheet, 1), QStringLiteral("absent"));
}

TEST_F(TrainingSessionViewModelTest, LoadIsRpeTimesMinutes) {
    seedSheet();
    const auto sheet = makeSheet();
    sheet->enterRpeDigit(0, 7);
    EXPECT_EQ(value(*sheet, 0, VM::LoadRole).toInt(), 7 * kDuration);
    sheet->setMinutesText(0, QStringLiteral("60"));
    EXPECT_EQ(value(*sheet, 0, VM::LoadRole).toInt(), 7 * 60);
    sheet->setMinutesText(0, QStringLiteral("0"));
    EXPECT_EQ(value(*sheet, 0, VM::LoadRole).toInt(), 0);
    sheet->clearRpe(0);
    EXPECT_FALSE(value(*sheet, 0, VM::LoadRole).isValid());
}

TEST_F(TrainingSessionViewModelTest, MinutesOutOfRangeShowAnErrorAndNoLoad) {
    seedSheet();
    const auto sheet = makeSheet();
    sheet->enterRpeDigit(0, 7);
    sheet->setMinutesText(0, QStringLiteral("241"));
    EXPECT_EQ(value(*sheet, 0, VM::MinutesRole).toInt(), 241);
    EXPECT_TRUE(value(*sheet, 0, VM::MinutesHasErrorRole).toBool());
    EXPECT_FALSE(value(*sheet, 0, VM::RowErrorRole).toString().isEmpty());
    EXPECT_FALSE(value(*sheet, 0, VM::LoadRole).isValid());
    EXPECT_EQ(sheet->firstErrorRow(), 0);

    sheet->setMinutesText(0, QStringLiteral("240"));
    EXPECT_FALSE(value(*sheet, 0, VM::MinutesHasErrorRole).toBool());
    EXPECT_TRUE(value(*sheet, 0, VM::RowErrorRole).toString().isEmpty());
    EXPECT_EQ(sheet->firstErrorRow(), -1);
}

TEST_F(TrainingSessionViewModelTest, SaveRefusesMinutesOutOfRangeAndWritesNothing) {
    seedSheet();
    const auto sheet = makeSheet();
    const SignalCount errors(sheet.get(), &VM::errorOccurred);
    sheet->enterRpeDigit(1, 5);
    sheet->setMinutesText(0, QStringLiteral("300"));
    EXPECT_FALSE(sheet->save());
    EXPECT_EQ(errors.count(), 1);
    EXPECT_TRUE(sheet->dirty());
    EXPECT_FALSE(value(*sheet, 0, VM::RowErrorRole).toString().isEmpty());
    const auto stored = registry().training().sheet(m_session);
    ASSERT_TRUE(stored.has_value());
    EXPECT_FALSE(stored->rows[0].saved);
    EXPECT_FALSE(stored->rows[1].saved);
}

TEST_F(TrainingSessionViewModelTest, StatusOtherThanPresentClearsAndDisables) {
    seedSheet();
    const auto sheet = makeSheet();
    for (const char* other : {"absent", "excused", "injured"}) {
        sheet->setStatus(0, QStringLiteral("present"));
        sheet->enterRpeDigit(0, 6);
        sheet->setMinutesText(0, QStringLiteral("45"));
        sheet->setStatus(0, QString::fromLatin1(other));
        EXPECT_FALSE(rpe(*sheet, 0).isValid()) << other;
        EXPECT_FALSE(value(*sheet, 0, VM::MinutesRole).isValid()) << other;
        EXPECT_FALSE(value(*sheet, 0, VM::LoadRole).isValid()) << other;
        EXPECT_FALSE(value(*sheet, 0, VM::CellsEnabledRole).toBool()) << other;
        // A disabled cell takes nothing.
        sheet->enterRpeDigit(0, 3);
        sheet->setMinutesText(0, QStringLiteral("10"));
        EXPECT_FALSE(rpe(*sheet, 0).isValid()) << other;
        EXPECT_FALSE(value(*sheet, 0, VM::MinutesRole).isValid()) << other;
    }
    sheet->setStatus(0, QStringLiteral("present"));
    EXPECT_FALSE(rpe(*sheet, 0).isValid());
    EXPECT_EQ(value(*sheet, 0, VM::MinutesRole).toInt(), kDuration);
}

TEST_F(TrainingSessionViewModelTest, MarkAllPresentSkipsThoseTheAvailabilityKeepsOut) {
    addSession(kToday);
    const PlayerId injured = addPlayer("Jan", "Kowalski", 1);
    const PlayerId ill = addPlayer("Adam", "Nowak", 2);
    const PlayerId away = addPlayer("Ewa", "Zych", 3);
    const PlayerId suspended = addPlayer("Ola", "Bak", 4);
    addPlayer("Iza", "Lis", 5);
    addAvailability(injured, AvailabilityType::Injury, kYesterday);
    addAvailability(ill, AvailabilityType::Illness, kYesterday);
    addAvailability(away, AvailabilityType::Absence, kToday, kToday);
    addAvailability(suspended, AvailabilityType::Suspension, kToday, kToday);
    const auto sheet = makeSheet();
    sheet->setStatus(3, QStringLiteral("absent"));
    sheet->setStatus(4, QStringLiteral("excused"));
    sheet->markAllPresent();
    EXPECT_EQ(status(*sheet, 0), QStringLiteral("injured"));
    EXPECT_EQ(status(*sheet, 1), QStringLiteral("injured"));
    EXPECT_EQ(status(*sheet, 2), QStringLiteral("excused"));
    EXPECT_EQ(status(*sheet, 3), QStringLiteral("present"));
    EXPECT_EQ(status(*sheet, 4), QStringLiteral("present"));
}

TEST_F(TrainingSessionViewModelTest, MarkAllPresentKeepsTheRpeOfThoseAlreadyPresent) {
    seedSheet();
    const auto sheet = makeSheet();
    sheet->enterRpeDigit(0, 8);
    sheet->setStatus(1, QStringLiteral("absent"));
    sheet->markAllPresent();
    EXPECT_EQ(rpe(*sheet, 0).toInt(), 8);
    EXPECT_EQ(status(*sheet, 1), QStringLiteral("present"));
}

TEST_F(TrainingSessionViewModelTest, MarkAllPresentCanBringAPlayerSavedAsAnInjuredBack) {
    seedSheet();
    const auto sheet = makeSheet();
    sheet->setStatus(0, QStringLiteral("injured"));
    ASSERT_TRUE(sheet->save());
    sheet->markAllPresent();
    EXPECT_EQ(status(*sheet, 0), QStringLiteral("present"));
    EXPECT_TRUE(sheet->dirty());
}

TEST_F(TrainingSessionViewModelTest, DigitSetsTheRpeAtOnce) {
    seedSheet();
    const auto sheet = makeSheet();
    for (int digit = 0; digit <= 9; ++digit) {
        sheet->enterRpeDigitAt(0, digit, (digit + 1) * 10000);
        EXPECT_EQ(rpe(*sheet, 0).toInt(), digit);
    }
}

TEST_F(TrainingSessionViewModelTest, OneThenZeroInTimeIsTen) {
    seedSheet();
    const auto sheet = makeSheet();
    sheet->enterRpeDigitAt(0, 1, 1000);
    EXPECT_EQ(rpe(*sheet, 0).toInt(), 1);
    sheet->enterRpeDigitAt(0, 0, 1000 + VM::kTenWindowMs);
    EXPECT_EQ(rpe(*sheet, 0).toInt(), 10);
}

TEST_F(TrainingSessionViewModelTest, OneThenZeroTooLateIsZero) {
    seedSheet();
    const auto sheet = makeSheet();
    sheet->enterRpeDigitAt(0, 1, 1000);
    sheet->enterRpeDigitAt(0, 0, 1000 + VM::kTenWindowMs + 1);
    EXPECT_EQ(rpe(*sheet, 0).toInt(), 0);
}

TEST_F(TrainingSessionViewModelTest, OneThenAnotherDigitIsThatDigit) {
    seedSheet();
    const auto sheet = makeSheet();
    sheet->enterRpeDigitAt(0, 1, 1000);
    sheet->enterRpeDigitAt(0, 5, 1100);
    EXPECT_EQ(rpe(*sheet, 0).toInt(), 5);
    sheet->enterRpeDigitAt(0, 0, 1200);
    EXPECT_EQ(rpe(*sheet, 0).toInt(), 0);
}

TEST_F(TrainingSessionViewModelTest, TenDoesNotChainIntoAnotherTen) {
    seedSheet();
    const auto sheet = makeSheet();
    sheet->enterRpeDigitAt(0, 1, 1000);
    sheet->enterRpeDigitAt(0, 0, 1100);
    sheet->enterRpeDigitAt(0, 0, 1200);
    EXPECT_EQ(rpe(*sheet, 0).toInt(), 0);
}

TEST_F(TrainingSessionViewModelTest, OneOneThenZeroIsTen) {
    seedSheet();
    const auto sheet = makeSheet();
    sheet->enterRpeDigitAt(0, 1, 1000);
    sheet->enterRpeDigitAt(0, 1, 1100);
    sheet->enterRpeDigitAt(0, 0, 1200);
    EXPECT_EQ(rpe(*sheet, 0).toInt(), 10);
}

TEST_F(TrainingSessionViewModelTest, ZeroInAnotherRowIsNotTen) {
    seedSheet();
    const auto sheet = makeSheet();
    sheet->enterRpeDigitAt(0, 1, 1000);
    sheet->enterRpeDigitAt(1, 0, 1100);
    EXPECT_EQ(rpe(*sheet, 0).toInt(), 1);
    EXPECT_EQ(rpe(*sheet, 1).toInt(), 0);
}

TEST_F(TrainingSessionViewModelTest, LeavingTheCellEndsTheTen) {
    seedSheet();
    const auto sheet = makeSheet();
    sheet->rememberCell(0, VM::RpeColumn);
    sheet->enterRpeDigitAt(0, 1, 1000);
    sheet->rememberCell(0, VM::MinutesColumn);
    sheet->rememberCell(0, VM::RpeColumn);
    sheet->enterRpeDigitAt(0, 0, 1100);
    EXPECT_EQ(rpe(*sheet, 0).toInt(), 0);
}

TEST_F(TrainingSessionViewModelTest, EscRevertsTheRpeToWhatTheCellHeld) {
    seedSheet();
    const auto sheet = makeSheet();
    sheet->enterRpeDigit(0, 4);
    sheet->rememberCell(0, VM::RpeColumn);
    sheet->enterRpeDigit(0, 9);
    sheet->revertCell(0, VM::RpeColumn);
    EXPECT_EQ(rpe(*sheet, 0).toInt(), 4);
}

TEST_F(TrainingSessionViewModelTest, EscOnAnEmptyCellEmptiesIt) {
    seedSheet();
    const auto sheet = makeSheet();
    sheet->rememberCell(0, VM::RpeColumn);
    sheet->enterRpeDigit(0, 9);
    sheet->revertCell(0, VM::RpeColumn);
    EXPECT_FALSE(rpe(*sheet, 0).isValid());
    EXPECT_FALSE(sheet->dirty());
}

TEST_F(TrainingSessionViewModelTest, EscRevertsTheMinutes) {
    seedSheet();
    const auto sheet = makeSheet();
    sheet->rememberCell(0, VM::MinutesColumn);
    sheet->setMinutesText(0, QStringLiteral("300"));
    sheet->revertCell(0, VM::MinutesColumn);
    EXPECT_EQ(value(*sheet, 0, VM::MinutesRole).toInt(), kDuration);
    EXPECT_FALSE(value(*sheet, 0, VM::MinutesHasErrorRole).toBool());
}

TEST_F(TrainingSessionViewModelTest, EscOnTheStatusBringsBackWhatTheChangeCleared) {
    seedSheet();
    const auto sheet = makeSheet();
    sheet->enterRpeDigit(0, 6);
    sheet->setMinutesText(0, QStringLiteral("45"));
    sheet->rememberCell(0, VM::StatusColumn);
    sheet->setStatus(0, QStringLiteral("absent"));
    sheet->revertCell(0, VM::StatusColumn);
    EXPECT_EQ(status(*sheet, 0), QStringLiteral("present"));
    EXPECT_EQ(rpe(*sheet, 0).toInt(), 6);
    EXPECT_EQ(value(*sheet, 0, VM::MinutesRole).toInt(), 45);
}

TEST_F(TrainingSessionViewModelTest, EscOnlyTouchesTheRememberedCell) {
    seedSheet();
    const auto sheet = makeSheet();
    sheet->rememberCell(0, VM::RpeColumn);
    sheet->enterRpeDigit(0, 9);
    sheet->revertCell(1, VM::RpeColumn);
    sheet->revertCell(0, VM::MinutesColumn);
    EXPECT_EQ(rpe(*sheet, 0).toInt(), 9);
}

TEST_F(TrainingSessionViewModelTest, EscAfterASaveDoesNotBringBackWhatCameBefore) {
    seedSheet();
    const auto sheet = makeSheet();
    sheet->rememberCell(0, VM::RpeColumn);
    sheet->enterRpeDigit(0, 9);
    ASSERT_TRUE(sheet->save());
    sheet->revertCell(0, VM::RpeColumn);
    EXPECT_EQ(rpe(*sheet, 0).toInt(), 9);
    EXPECT_FALSE(sheet->dirty());
}

TEST_F(TrainingSessionViewModelTest, DirtyFollowsTheChangesBothWays) {
    seedSheet();
    const auto sheet = makeSheet();
    const SignalCount notified(sheet.get(), &VM::dirtyChanged);
    EXPECT_FALSE(sheet->dirty());
    sheet->enterRpeDigit(0, 5);
    EXPECT_TRUE(sheet->dirty());
    EXPECT_EQ(notified.count(), 1);
    sheet->clearRpe(0);
    EXPECT_FALSE(sheet->dirty());
    EXPECT_EQ(notified.count(), 2);
    sheet->setStatus(1, QStringLiteral("excused"));
    EXPECT_TRUE(sheet->dirty());
    sheet->setStatus(1, QStringLiteral("present"));
    EXPECT_FALSE(sheet->dirty());
}

TEST_F(TrainingSessionViewModelTest, TypingTheDurationIsNoChange) {
    seedSheet();
    const auto sheet = makeSheet();
    sheet->setMinutesText(0, QStringLiteral("90"));
    EXPECT_FALSE(sheet->dirty());
    sheet->setMinutesText(0, QStringLiteral("45"));
    EXPECT_TRUE(sheet->dirty());
    sheet->setMinutesText(0, QString{});
    EXPECT_FALSE(sheet->dirty());
    EXPECT_EQ(value(*sheet, 0, VM::MinutesRole).toInt(), kDuration);
}

TEST_F(TrainingSessionViewModelTest, MinutesEqualToTheDurationAreStoredEmpty) {
    seedSheet();
    const auto sheet = makeSheet();
    sheet->enterRpeDigit(0, 5);
    sheet->setMinutesText(0, QStringLiteral("90"));
    ASSERT_TRUE(sheet->save());
    const auto stored = registry().training().sheet(m_session);
    ASSERT_TRUE(stored.has_value());
    EXPECT_FALSE(stored->rows[0].draft.minutesOverride.has_value());
}

TEST_F(TrainingSessionViewModelTest, NeverSavedSheetCanBeSavedWithoutAChange) {
    seedSheet();
    const auto sheet = makeSheet();
    EXPECT_FALSE(sheet->dirty());
    EXPECT_TRUE(sheet->canSave());
    ASSERT_TRUE(sheet->save());
    EXPECT_FALSE(sheet->dirty());
    EXPECT_FALSE(sheet->canSave());

    const auto stored = registry().training().sheet(m_session);
    ASSERT_TRUE(stored.has_value());
    EXPECT_TRUE(stored->rows[0].saved);
    EXPECT_TRUE(stored->rows[1].saved);
}

TEST_F(TrainingSessionViewModelTest, SavedSheetCanOnlyBeSavedAfterAChange) {
    seedSheet();
    {
        const auto sheet = makeSheet();
        ASSERT_TRUE(sheet->save());
    }
    const auto sheet = makeSheet();
    EXPECT_FALSE(sheet->canSave());
    sheet->enterRpeDigit(0, 5);
    EXPECT_TRUE(sheet->canSave());
    sheet->clearRpe(0);
    EXPECT_FALSE(sheet->canSave());
}

TEST_F(TrainingSessionViewModelTest, PlayerAddedAfterTheSaveMakesTheSheetUnsaved) {
    seedSheet();
    {
        const auto sheet = makeSheet();
        ASSERT_TRUE(sheet->save());
        EXPECT_EQ(sheet->unsavedCount(), 0);
        EXPECT_TRUE(sheet->unsavedText().isEmpty());
    }
    addPlayer("Ewa", "Zych", 11);
    const auto sheet = makeSheet();
    ASSERT_EQ(sheet->count(), 3);
    EXPECT_EQ(sheet->unsavedCount(), 1);
    EXPECT_EQ(sheet->unsavedText(), QStringLiteral("Not saved: 1 player"));
    EXPECT_TRUE(sheet->canSave());
    // A default is nothing the user did: leaving does not ask.
    EXPECT_FALSE(sheet->dirty());

    ASSERT_TRUE(sheet->save());
    EXPECT_EQ(sheet->unsavedCount(), 0);
    EXPECT_FALSE(sheet->canSave());
    const auto stored = registry().training().sheet(m_session);
    ASSERT_TRUE(stored.has_value());
    EXPECT_TRUE(std::ranges::all_of(stored->rows, [](const auto& row) { return row.saved; }));
}

TEST_F(TrainingSessionViewModelTest, PlayerAddedWhileTheSheetIsOpenCountsAfterAReload) {
    seedSheet();
    const auto sheet = makeSheet();
    ASSERT_TRUE(sheet->save());
    addPlayer("Ewa", "Zych", 11);
    sheet->reload();
    EXPECT_EQ(sheet->unsavedCount(), 1);
    EXPECT_TRUE(sheet->canSave());
    EXPECT_FALSE(sheet->dirty());
}

TEST_F(TrainingSessionViewModelTest, PartlySavedSheetCountsTheRestAndCanBeSaved) {
    seedSheet();
    addPlayer("Ewa", "Zych", 11);
    AttendanceDraft saved;
    saved.playerId = m_first;
    saved.rpe = 6;
    const std::vector<AttendanceDraft> rows{saved};
    ASSERT_TRUE(registry().training().saveAttendance(m_session, rows).has_value());

    const auto sheet = makeSheet();
    EXPECT_EQ(rpe(*sheet, 0).toInt(), 6);
    EXPECT_EQ(sheet->unsavedCount(), 2);
    EXPECT_EQ(sheet->unsavedText(), QStringLiteral("Not saved: 2 players"));
    EXPECT_TRUE(sheet->canSave());
    EXPECT_FALSE(sheet->dirty());

    ASSERT_TRUE(sheet->save());
    EXPECT_EQ(sheet->unsavedCount(), 0);
    EXPECT_EQ(rpe(*sheet, 0).toInt(), 6);
}

TEST_F(TrainingSessionViewModelTest, ChangedRowsCountAsUnsavedAndGoBackWithEsc) {
    seedSheet();
    const auto sheet = makeSheet();
    EXPECT_EQ(sheet->unsavedCount(), 2);
    ASSERT_TRUE(sheet->save());
    const SignalCount notified(sheet.get(), &VM::unsavedChanged);
    sheet->rememberCell(0, VM::RpeColumn);
    sheet->enterRpeDigit(0, 5);
    EXPECT_EQ(sheet->unsavedCount(), 1);
    EXPECT_EQ(notified.count(), 1);
    sheet->revertCell(0, VM::RpeColumn);
    EXPECT_EQ(sheet->unsavedCount(), 0);
    EXPECT_EQ(notified.count(), 2);
    EXPECT_FALSE(sheet->canSave());
}

TEST_F(TrainingSessionViewModelTest, DiscardLeavesRowsWithoutASaveUnsaved) {
    seedSheet();
    const auto sheet = makeSheet();
    sheet->enterRpeDigit(0, 5);
    sheet->discardChanges();
    EXPECT_FALSE(sheet->dirty());
    EXPECT_EQ(sheet->unsavedCount(), 2);
    EXPECT_TRUE(sheet->canSave());
}

TEST_F(TrainingSessionViewModelTest, FailedSaveMarksNothingAsSaved) {
    seedSheet();
    const auto sheet = makeSheet();
    sheet->setMinutesText(0, QStringLiteral("300"));
    EXPECT_FALSE(sheet->save());
    EXPECT_EQ(sheet->unsavedCount(), 2);
}

TEST_F(TrainingSessionViewModelTest, ReadOnlyRoleCannotSaveAnUnsavedSheet) {
    seedSheet();
    logInAs(Role::Analyst);
    const auto sheet = makeSheet();
    EXPECT_EQ(sheet->unsavedCount(), 2);
    EXPECT_FALSE(sheet->canSave());
}

TEST_F(TrainingSessionViewModelTest, SheetWithoutPlayersCannotBeSaved) {
    addSession(kToday);
    const auto sheet = makeSheet();
    EXPECT_EQ(sheet->count(), 0);
    EXPECT_FALSE(sheet->canSave());
}

TEST_F(TrainingSessionViewModelTest, SavedValuesComeBackTheSame) {
    seedSheet();
    {
        const auto sheet = makeSheet();
        sheet->enterRpeDigitAt(0, 1, 1000);
        sheet->enterRpeDigitAt(0, 0, 1100);
        sheet->setMinutesText(0, QStringLiteral("75"));
        sheet->setStatus(1, QStringLiteral("excused"));
        ASSERT_TRUE(sheet->save());
    }
    const auto sheet = makeSheet();
    EXPECT_EQ(rpe(*sheet, 0).toInt(), 10);
    EXPECT_EQ(value(*sheet, 0, VM::MinutesRole).toInt(), 75);
    EXPECT_EQ(value(*sheet, 0, VM::LoadRole).toInt(), 750);
    EXPECT_EQ(status(*sheet, 1), QStringLiteral("excused"));
    EXPECT_FALSE(sheet->dirty());
}

TEST_F(TrainingSessionViewModelTest, SaveAndReloadKeepTheDelegates) {
    seedSheet();
    const auto sheet = makeSheet();
    const SignalCount resets(sheet.get(), &QAbstractItemModel::modelReset);
    const SignalCount saved(sheet.get(), &VM::saved);
    sheet->enterRpeDigit(0, 5);
    ASSERT_TRUE(sheet->save());
    EXPECT_EQ(saved.count(), 1);
    sheet->reload();
    EXPECT_EQ(resets.count(), 0);
}

TEST_F(TrainingSessionViewModelTest, ReloadKeepsTheChangesOfTheUser) {
    seedSheet();
    const auto sheet = makeSheet();
    sheet->enterRpeDigit(0, 5);
    sheet->reload();
    EXPECT_TRUE(sheet->dirty());
    EXPECT_EQ(rpe(*sheet, 0).toInt(), 5);
}

TEST_F(TrainingSessionViewModelTest, ReloadFollowsTheDurationOfTheSession) {
    seedSheet();
    const auto sheet = makeSheet();
    TrainingSessionDraft draft;
    draft.date = kToday;
    draft.durationMin = 60;
    ASSERT_TRUE(registry().calendar().updateSession(m_session, draft).has_value());
    sheet->reload();
    EXPECT_EQ(sheet->durationMin(), 60);
    EXPECT_EQ(value(*sheet, 0, VM::MinutesRole).toInt(), 60);
    EXPECT_FALSE(sheet->dirty());
}

TEST_F(TrainingSessionViewModelTest, ReloadKeepsTheChosenSession) {
    seedSheet();
    const TrainingSessionId older = addSession(kLastWeek);
    const auto sheet = makeSheet();
    EXPECT_EQ(sheet->sessionId(), m_session.value);
    sheet->openSession(older.value);
    sheet->reload();
    EXPECT_EQ(sheet->sessionId(), older.value);
}

TEST_F(TrainingSessionViewModelTest, ReloadMovesOnWhenTheSessionIsGone) {
    seedSheet();
    const TrainingSessionId older = addSession(kLastWeek);
    const auto sheet = makeSheet();
    sheet->openSession(older.value);
    ASSERT_TRUE(registry().calendar().deleteSession(older).has_value());
    sheet->reload();
    EXPECT_EQ(sheet->sessionId(), m_session.value);
}

// The picker in the view goes back to the open session (showValue), so what is on the screen and
// what a save writes stay the same session.
TEST_F(TrainingSessionViewModelTest, ASessionThatCannotBeReadLeavesTheOpenOne) {
    seedSheet();
    const TrainingSessionId older = addSession(kLastWeek);
    const auto sheet = makeSheet();
    ASSERT_EQ(sheet->sessionId(), m_session.value);
    const SignalCount errors(sheet.get(), &VM::errorOccurred);
    const SignalCount resets(sheet.get(), &QAbstractItemModel::modelReset);
    ASSERT_TRUE(
        m_db->execSql(QStringLiteral("ALTER TABLE attendance RENAME TO attendance_gone"), "test")
            .has_value());

    sheet->openSession(older.value);

    EXPECT_EQ(errors.count(), 1);
    EXPECT_EQ(sheet->sessionId(), m_session.value);
    EXPECT_TRUE(sheet->loadError().isEmpty());
    EXPECT_EQ(sheet->count(), 2);
    EXPECT_EQ(resets.count(), 0);

    ASSERT_TRUE(
        m_db->execSql(QStringLiteral("ALTER TABLE attendance_gone RENAME TO attendance"), "test")
            .has_value());
    sheet->enterRpeDigit(0, 7);
    ASSERT_TRUE(sheet->save());
    const auto shown = registry().training().sheet(m_session);
    ASSERT_TRUE(shown.has_value());
    EXPECT_TRUE(std::ranges::all_of(shown->rows, [](const auto& row) { return row.saved; }));
    const auto other = registry().training().sheet(older);
    ASSERT_TRUE(other.has_value());
    EXPECT_TRUE(std::ranges::none_of(other->rows, [](const auto& row) { return row.saved; }));
}

TEST_F(TrainingSessionViewModelTest, NothingIsSavedWhileTheSheetCannotBeRead) {
    seedSheet();
    const auto sheet = makeSheet();
    ASSERT_TRUE(sheet->canSave());
    ASSERT_TRUE(
        m_db->execSql(QStringLiteral("ALTER TABLE attendance RENAME TO attendance_gone"), "test")
            .has_value());
    sheet->reload();
    ASSERT_FALSE(sheet->loadError().isEmpty());
    EXPECT_FALSE(sheet->canSave());
    EXPECT_FALSE(sheet->save());

    ASSERT_TRUE(
        m_db->execSql(QStringLiteral("ALTER TABLE attendance_gone RENAME TO attendance"), "test")
            .has_value());
    sheet->reload();
    EXPECT_TRUE(sheet->loadError().isEmpty());
    EXPECT_TRUE(sheet->canSave());
}

TEST_F(TrainingSessionViewModelTest, OpeningAnotherSessionDropsTheChanges) {
    seedSheet();
    const TrainingSessionId older = addSession(kLastWeek);
    const auto sheet = makeSheet();
    sheet->enterRpeDigit(0, 5);
    sheet->openSession(older.value);
    EXPECT_EQ(sheet->sessionId(), older.value);
    EXPECT_FALSE(sheet->dirty());
    EXPECT_FALSE(rpe(*sheet, 0).isValid());
}

TEST_F(TrainingSessionViewModelTest, DiscardBringsBackWhatWasSaved) {
    seedSheet();
    const auto sheet = makeSheet();
    sheet->enterRpeDigit(0, 5);
    ASSERT_TRUE(sheet->save());
    sheet->enterRpeDigit(0, 8);
    sheet->setStatus(1, QStringLiteral("absent"));
    sheet->discardChanges();
    EXPECT_EQ(rpe(*sheet, 0).toInt(), 5);
    EXPECT_EQ(status(*sheet, 1), QStringLiteral("present"));
    EXPECT_FALSE(sheet->dirty());
}

TEST_F(TrainingSessionViewModelTest, AnalystReadsAndChangesNothing) {
    seedSheet();
    logInAs(Role::Analyst);
    const auto sheet = makeSheet();
    const SignalCount errors(sheet.get(), &VM::errorOccurred);
    EXPECT_TRUE(sheet->readOnly());
    EXPECT_EQ(sheet->count(), 2);
    EXPECT_FALSE(sheet->canSave());
    EXPECT_FALSE(value(*sheet, 0, VM::CellsEnabledRole).toBool());

    sheet->enterRpeDigit(0, 5);
    sheet->clearRpe(0);
    sheet->setMinutesText(0, QStringLiteral("10"));
    sheet->setStatus(0, QStringLiteral("absent"));
    sheet->markAllPresent();
    EXPECT_FALSE(sheet->dirty());
    EXPECT_FALSE(sheet->save());
    EXPECT_EQ(errors.count(), 1);
}

TEST_F(TrainingSessionViewModelTest, PhysioMayEdit) {
    seedSheet();
    logInAs(Role::Physio);
    const auto sheet = makeSheet();
    EXPECT_FALSE(sheet->readOnly());
    sheet->enterRpeDigit(0, 5);
    EXPECT_TRUE(sheet->save());
}

TEST_F(TrainingSessionViewModelTest, StatusOptionsAreTheFourStatuses) {
    seedSheet();
    const auto sheet = makeSheet();
    const QVariantList options = sheet->statusOptions();
    ASSERT_EQ(options.size(), 4);
    EXPECT_EQ(options[0].toMap().value(QStringLiteral("value")).toString(),
              QStringLiteral("present"));
    EXPECT_FALSE(options[3].toMap().value(QStringLiteral("text")).toString().isEmpty());
}

TEST_F(TrainingSessionViewModelTest, UnknownStatusChangesNothing) {
    seedSheet();
    const auto sheet = makeSheet();
    sheet->setStatus(0, QStringLiteral("nope"));
    EXPECT_EQ(status(*sheet, 0), QStringLiteral("present"));
    EXPECT_FALSE(sheet->dirty());
}

} // namespace
