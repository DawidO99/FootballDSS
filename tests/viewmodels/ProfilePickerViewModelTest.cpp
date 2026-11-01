#include "viewmodels/ProfilePickerViewModel.hpp"

#include "ViewModelFixture.hpp"
#include "services/Session.hpp"
#include "viewmodels/AppState.hpp"

#include <gtest/gtest.h>

#include <chrono>

using namespace fdss::core;
using namespace fdss::tests;
using fdss::viewmodels::AppState;
using fdss::viewmodels::ProfilePickerViewModel;
using fdss::viewmodels::StaffListModel;

namespace {

constexpr auto kWrongPin = "9999";

class ProfilePickerViewModelTest : public ViewModelTest {
protected:
    // A Coach and an Assistant, nobody logged in.
    void SetUp() override {
        ViewModelTest::SetUp();
        m_coach = completeSetup().id;
        m_assistant = addStaff("Anna Asystent", Role::Assistant);
        registry().auth().logout();
    }

    static void failLogin(ProfilePickerViewModel& picker, int times) {
        for (int i = 0; i < times; ++i) {
            picker.login(QString::fromLatin1(kWrongPin));
        }
    }

    StaffUserId m_coach;
    StaffUserId m_assistant;
};

} // namespace

TEST_F(ProfilePickerViewModelTest, ListsTheActiveStaffWithNobodySelected) {
    registry().auth().logout();
    const ProfilePickerViewModel picker;

    const StaffListModel* staff = picker.staff();
    ASSERT_EQ(staff->rowCount(), 2);
    EXPECT_EQ(picker.selectedId(), 0);
    EXPECT_TRUE(picker.selectedName().isEmpty());
    EXPECT_TRUE(picker.message().isEmpty());
    EXPECT_FALSE(picker.locked());
    EXPECT_TRUE(picker.loadError().isEmpty());
}

TEST_F(ProfilePickerViewModelTest, ADeactivatedUserHasNoTile) {
    ASSERT_TRUE(registry().auth().login(m_coach, kPin).has_value());
    ASSERT_TRUE(registry().auth().deactivate(m_assistant).has_value());
    registry().auth().logout();

    const ProfilePickerViewModel picker;

    EXPECT_EQ(picker.staff()->rowCount(), 1);
    EXPECT_EQ(picker.staff()->find(m_assistant.value), nullptr);
}

TEST_F(ProfilePickerViewModelTest, ASingleUserIsSelectedRightAway) {
    ASSERT_TRUE(registry().auth().login(m_coach, kPin).has_value());
    ASSERT_TRUE(registry().auth().deactivate(m_assistant).has_value());
    registry().auth().logout();

    const ProfilePickerViewModel picker;

    EXPECT_EQ(picker.selectedId(), m_coach.value);
    EXPECT_EQ(picker.selectedName(), QStringLiteral("Jan Trener"));
}

TEST_F(ProfilePickerViewModelTest, TheRightPinStartsTheSession) {
    AppState state;
    ASSERT_EQ(state.state(), AppState::State::NeedsLogin);
    ProfilePickerViewModel picker;
    const SignalCount loggedIn(&picker, &ProfilePickerViewModel::loggedIn);
    picker.select(m_assistant.value);

    picker.login(QString::fromLatin1(kPin));

    EXPECT_EQ(loggedIn.count(), 1);
    state.refresh();
    EXPECT_EQ(state.state(), AppState::State::Ready);
    EXPECT_EQ(state.userName(), QStringLiteral("Anna Asystent"));
    EXPECT_FALSE(state.can(QStringLiteral("ManageStaff")));
}

TEST_F(ProfilePickerViewModelTest, AWrongPinNamesTheAttemptsLeft) {
    ProfilePickerViewModel picker;
    const SignalCount rejected(&picker, &ProfilePickerViewModel::pinRejected);
    picker.select(m_coach.value);

    failLogin(picker, 1);
    EXPECT_EQ(picker.message(), QStringLiteral("Wrong PIN. Attempts left: 4."));

    failLogin(picker, 3);
    EXPECT_EQ(picker.message(), QStringLiteral("Wrong PIN. Attempts left: 1."));
    EXPECT_FALSE(picker.locked());
    EXPECT_EQ(rejected.count(), 4);
    EXPECT_FALSE(registry().session().user().has_value());
}

TEST_F(ProfilePickerViewModelTest, AMalformedPinIsNotCounted) {
    ProfilePickerViewModel picker;
    const SignalCount rejected(&picker, &ProfilePickerViewModel::pinRejected);
    picker.select(m_coach.value);

    picker.login(QStringLiteral("12"));
    EXPECT_EQ(picker.message(), QStringLiteral("A PIN has 4 to 8 digits."));
    EXPECT_EQ(rejected.count(), 0);

    failLogin(picker, 1);
    EXPECT_EQ(picker.message(), QStringLiteral("Wrong PIN. Attempts left: 4."));
}

TEST_F(ProfilePickerViewModelTest, TheFifthWrongPinShowsTheLockout) {
    ProfilePickerViewModel picker;
    picker.select(m_coach.value);

    failLogin(picker, 5);

    EXPECT_TRUE(picker.locked());
    EXPECT_EQ(picker.message(), QStringLiteral("Too many wrong PINs. Try again in 30 s."));
}

TEST_F(ProfilePickerViewModelTest, TheLockoutCountsDownAndEnds) {
    ProfilePickerViewModel picker;
    picker.select(m_coach.value);
    failLogin(picker, 5);

    m_clock->advance(std::chrono::seconds{12});
    picker.updateLock();
    EXPECT_TRUE(picker.locked());
    EXPECT_EQ(picker.message(), QStringLiteral("Too many wrong PINs. Try again in 18 s."));

    m_clock->advance(std::chrono::seconds{18});
    picker.updateLock();
    EXPECT_FALSE(picker.locked());
    EXPECT_TRUE(picker.message().isEmpty());

    picker.login(QString::fromLatin1(kPin));
    EXPECT_TRUE(registry().session().user().has_value());
}

TEST_F(ProfilePickerViewModelTest, TheLockoutSurvivesARestart) {
    {
        ProfilePickerViewModel picker;
        picker.select(m_coach.value);
        failLogin(picker, 5);
    }
    m_clock->advance(std::chrono::seconds{10});

    ProfilePickerViewModel afterRestart;
    EXPECT_FALSE(afterRestart.locked());
    afterRestart.select(m_coach.value);

    EXPECT_TRUE(afterRestart.locked());
    EXPECT_EQ(afterRestart.message(), QStringLiteral("Too many wrong PINs. Try again in 20 s."));
}

TEST_F(ProfilePickerViewModelTest, AnAttemptDuringALockoutDoesNotExtendIt) {
    ProfilePickerViewModel picker;
    const SignalCount rejected(&picker, &ProfilePickerViewModel::pinRejected);
    picker.select(m_coach.value);
    failLogin(picker, 5);
    m_clock->advance(std::chrono::seconds{10});

    picker.login(QString::fromLatin1(kPin));

    EXPECT_EQ(picker.message(), QStringLiteral("Too many wrong PINs. Try again in 20 s."));
    EXPECT_EQ(rejected.count(), 6);
    EXPECT_FALSE(registry().session().user().has_value());
}

TEST_F(ProfilePickerViewModelTest, AnotherProfileIsNotLockedOrToldAboutTheFirst) {
    ProfilePickerViewModel picker;
    picker.select(m_coach.value);
    failLogin(picker, 5);
    ASSERT_TRUE(picker.locked());

    picker.select(m_assistant.value);
    EXPECT_FALSE(picker.locked());
    EXPECT_TRUE(picker.message().isEmpty());

    picker.select(m_coach.value);
    EXPECT_TRUE(picker.locked());
}

TEST_F(ProfilePickerViewModelTest, ClearingTheSelectionClearsTheMessage) {
    ProfilePickerViewModel picker;
    picker.select(m_coach.value);
    failLogin(picker, 1);

    picker.select(0);

    EXPECT_EQ(picker.selectedId(), 0);
    EXPECT_TRUE(picker.message().isEmpty());
}

TEST_F(ProfilePickerViewModelTest, LoginWithNobodySelectedDoesNothing) {
    ProfilePickerViewModel picker;
    const SignalCount loggedIn(&picker, &ProfilePickerViewModel::loggedIn);

    picker.login(QString::fromLatin1(kPin));

    EXPECT_EQ(loggedIn.count(), 0);
    EXPECT_TRUE(picker.message().isEmpty());
}

TEST_F(ProfilePickerViewModelTest, AUserDeactivatedInTheMeantimeLeavesTheList) {
    ProfilePickerViewModel picker;
    QString message;
    QObject::connect(&picker, &ProfilePickerViewModel::errorOccurred, &picker,
                     [&](const QString& text) { message = text; });
    picker.select(m_assistant.value);
    ASSERT_TRUE(registry().auth().login(m_coach, kPin).has_value());
    ASSERT_TRUE(registry().auth().deactivate(m_assistant).has_value());
    registry().auth().logout();

    picker.login(QString::fromLatin1(kPin));

    EXPECT_FALSE(message.isEmpty());
    EXPECT_EQ(picker.staff()->rowCount(), 1);
    EXPECT_EQ(picker.selectedId(), m_coach.value);
}

TEST_F(ProfilePickerViewModelTest, AnUnreadableListSaysSoAndCanBeTriedAgain) {
    ASSERT_TRUE(m_db->execSql(QStringLiteral("ALTER TABLE staff_user RENAME TO staff_gone"),
                              "break the database")
                    .has_value());
    ProfilePickerViewModel picker;

    EXPECT_FALSE(picker.loadError().isEmpty());
    EXPECT_FALSE(picker.loadError().contains(QLatin1StringView("staff_user")));
    EXPECT_EQ(picker.staff()->rowCount(), 0);

    ASSERT_TRUE(m_db->execSql(QStringLiteral("ALTER TABLE staff_gone RENAME TO staff_user"),
                              "repair the database")
                    .has_value());
    picker.reload();

    EXPECT_TRUE(picker.loadError().isEmpty());
    EXPECT_EQ(picker.staff()->rowCount(), 2);
}
