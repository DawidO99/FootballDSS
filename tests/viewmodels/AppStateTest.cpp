#include "viewmodels/AppState.hpp"

#include "ViewModelFixture.hpp"
#include "core/domain/Permissions.hpp"
#include "services/Session.hpp"

#include <QCoreApplication>
#include <QEvent>

#include <gtest/gtest.h>

#include <string>

using namespace fdss::core;
using namespace fdss::services;
using namespace fdss::tests;
using fdss::viewmodels::AppState;
using State = AppState::State;

namespace {

class AppStateTest : public ViewModelTest {};

} // namespace

TEST_F(AppStateTest, EmptyDatabaseNeedsSetup) {
    const AppState state;

    EXPECT_EQ(state.state(), State::NeedsSetup);
    EXPECT_TRUE(state.userName().isEmpty());
    EXPECT_TRUE(state.roleName().isEmpty());
    EXPECT_TRUE(state.clubName().isEmpty());
    EXPECT_TRUE(state.teamName().isEmpty());
    EXPECT_EQ(state.teamId(), 0);
}

TEST_F(AppStateTest, ExistingClubWithNobodyLoggedInNeedsLogin) {
    completeSetup();
    registry().auth().logout();

    const AppState state;

    EXPECT_EQ(state.state(), State::NeedsLogin);
    EXPECT_TRUE(state.userName().isEmpty());
    EXPECT_EQ(state.clubName(), QStringLiteral("KS Test"));
    EXPECT_EQ(state.teamName(), QStringLiteral("Seniorzy"));
    EXPECT_GT(state.teamId(), 0);
}

TEST_F(AppStateTest, RefreshAfterSetupIsReady) {
    AppState state;
    int stateChanges = 0;
    int userChanges = 0;
    int clubChanges = 0;
    QObject::connect(&state, &AppState::stateChanged, &state, [&] { ++stateChanges; });
    QObject::connect(&state, &AppState::userChanged, &state, [&] { ++userChanges; });
    QObject::connect(&state, &AppState::clubChanged, &state, [&] { ++clubChanges; });

    completeSetup();
    state.refresh();

    EXPECT_EQ(state.state(), State::Ready);
    EXPECT_EQ(state.userName(), QStringLiteral("Jan Trener"));
    EXPECT_EQ(state.roleName(), AppState::roleDisplayName(Role::Coach));
    EXPECT_EQ(state.clubName(), QStringLiteral("KS Test"));
    EXPECT_EQ(stateChanges, 1);
    EXPECT_EQ(userChanges, 1);
    EXPECT_EQ(clubChanges, 1);
}

// A view that the new state brings up reads these properties at once.
TEST_F(AppStateTest, UserAndClubAreSetBeforeTheStateChanges) {
    AppState state;
    QString userAtChange;
    QString clubAtChange;
    QObject::connect(&state, &AppState::stateChanged, &state, [&] {
        userAtChange = state.userName();
        clubAtChange = state.clubName();
    });

    completeSetup();
    state.refresh();

    EXPECT_EQ(userAtChange, QStringLiteral("Jan Trener"));
    EXPECT_EQ(clubAtChange, QStringLiteral("KS Test"));
}

TEST_F(AppStateTest, RefreshWithoutAnyChangeEmitsNothing) {
    completeSetup();
    AppState state;
    int changes = 0;
    QObject::connect(&state, &AppState::stateChanged, &state, [&] { ++changes; });
    QObject::connect(&state, &AppState::userChanged, &state, [&] { ++changes; });
    QObject::connect(&state, &AppState::clubChanged, &state, [&] { ++changes; });

    state.refresh();

    EXPECT_EQ(changes, 0);
}

TEST_F(AppStateTest, LogoutNeedsLoginAndClearsTheUser) {
    completeSetup();
    AppState state;
    ASSERT_EQ(state.state(), State::Ready);

    state.logout();

    EXPECT_EQ(state.state(), State::NeedsLogin);
    EXPECT_TRUE(state.userName().isEmpty());
    EXPECT_TRUE(state.roleName().isEmpty());
    EXPECT_FALSE(state.can(QStringLiteral("ViewAnalysis")));
    // The club is still there for the login screen.
    EXPECT_EQ(state.clubName(), QStringLiteral("KS Test"));
}

TEST_F(AppStateTest, CanFollowsTheMatrixForEveryRole) {
    const StaffUser coach = completeSetup();
    const StaffUserId assistant = addStaff("Asystent", Role::Assistant);
    const StaffUserId physio = addStaff("Fizjo", Role::Physio);
    const StaffUserId analyst = addStaff("Analityk", Role::Analyst);

    const auto check = [](StaffUserId id, Role role) {
        registry().auth().logout();
        const auto login = registry().auth().login(id, kPin);
        ASSERT_TRUE(login.has_value()) << login.error().detail;
        ASSERT_EQ(login->status, PinCheckStatus::Accepted);

        AppState state;
        EXPECT_EQ(state.roleName(), AppState::roleDisplayName(role));
        for (const Permission permission : allValues<Permission>()) {
            const QString name = QString::fromUtf8(toString(permission));
            EXPECT_EQ(state.can(name), isAllowed(role, permission))
                << toDbText(role) << " " << toString(permission);
        }
    };

    check(coach.id, Role::Coach);
    check(assistant, Role::Assistant);
    check(physio, Role::Physio);
    check(analyst, Role::Analyst);
}

TEST_F(AppStateTest, CanIsFalseForEveryPermissionWhenLoggedOut) {
    completeSetup();
    registry().auth().logout();
    const AppState state;

    for (const Permission permission : allValues<Permission>()) {
        EXPECT_FALSE(state.can(QString::fromUtf8(toString(permission)))) << toString(permission);
    }
}

TEST_F(AppStateTest, CanIsFalseForAnUnknownPermission) {
    completeSetup();
    const AppState state;

    EXPECT_FALSE(state.can(QStringLiteral("NoSuchPermission")));
    // Names are exact: QML must not get away with a typo.
    EXPECT_FALSE(state.can(QStringLiteral("managestaff")));
}

TEST_F(AppStateTest, EveryRoleHasADisplayName) {
    for (const Role role : allValues<Role>()) {
        EXPECT_FALSE(AppState::roleDisplayName(role).isEmpty()) << toDbText(role);
    }
}

TEST_F(AppStateTest, RenamingTheSessionUserShowsAfterRefresh) {
    const StaffUser coach = completeSetup();
    AppState state;
    StaffUserDraft draft = coach;
    draft.name = "Jan Nowy";

    ASSERT_TRUE(registry().auth().updateStaff(coach.id, draft).has_value());
    state.refresh();

    EXPECT_EQ(state.state(), State::Ready);
    EXPECT_EQ(state.userName(), QStringLiteral("Jan Nowy"));
}

TEST_F(AppStateTest, DemotingTheSessionUserNeedsLoginAfterRefresh) {
    const StaffUser coach = completeSetup();
    addStaff("Ewa", Role::Coach);
    AppState state;
    StaffUserDraft draft = coach;
    draft.role = Role::Assistant;

    ASSERT_TRUE(registry().auth().updateStaff(coach.id, draft).has_value());
    state.refresh();

    EXPECT_EQ(state.state(), State::NeedsLogin);
    EXPECT_TRUE(state.userName().isEmpty());
}

TEST_F(AppStateTest, FailedReadReportsAndKeepsTheState) {
    completeSetup();
    AppState state;
    ASSERT_EQ(state.state(), State::Ready);
    QString message;
    QObject::connect(&state, &AppState::errorOccurred, &state,
                     [&](const QString& text) { message = text; });
    ASSERT_TRUE(
        m_db->execSql(QStringLiteral("ALTER TABLE club RENAME TO club_gone"), "break").has_value());

    state.refresh();

    EXPECT_FALSE(message.isEmpty());
    EXPECT_FALSE(message.contains(QLatin1StringView("club_gone")));
    EXPECT_EQ(state.state(), State::Ready);
}

TEST_F(AppStateTest, LanguageChangeNotifiesTheRoleName) {
    completeSetup();
    AppState state;
    int changes = 0;
    QObject::connect(&state, &AppState::userChanged, &state, [&] { ++changes; });

    QEvent event(QEvent::LanguageChange);
    QCoreApplication::sendEvent(QCoreApplication::instance(), &event);

    EXPECT_EQ(changes, 1);
}

#ifndef QT_NO_DEBUG
TEST_F(AppStateTest, TestClubButtonGoesFromSetupToReady) {
    AppState state;
    ASSERT_TRUE(state.debugBuild());

    state.createTestClubAndLogin();

    EXPECT_EQ(state.state(), State::Ready);
    EXPECT_TRUE(state.can(QStringLiteral("ManageStaff")));
}

TEST_F(AppStateTest, TestClubButtonLogsTheCoachBackIn) {
    AppState state;
    state.createTestClubAndLogin();
    state.logout();
    ASSERT_EQ(state.state(), State::NeedsLogin);

    state.createTestClubAndLogin();

    EXPECT_EQ(state.state(), State::Ready);
    EXPECT_TRUE(state.can(QStringLiteral("ManageStaff")));
}

TEST_F(AppStateTest, TestClubButtonReportsACoachWithAnotherPin) {
    AppState state;
    state.createTestClubAndLogin();
    ASSERT_EQ(registry().auth().changeOwnPin(kPin, "5678").value_or(PinCheck{}).status,
              PinCheckStatus::Accepted);
    state.logout();
    QString message;
    QObject::connect(&state, &AppState::errorOccurred, &state,
                     [&](const QString& text) { message = text; });

    state.createTestClubAndLogin();

    EXPECT_FALSE(message.isEmpty());
    EXPECT_EQ(state.state(), State::NeedsLogin);
}
#endif
