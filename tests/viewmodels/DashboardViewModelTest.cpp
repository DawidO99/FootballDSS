#include "viewmodels/DashboardViewModel.hpp"

#include "ViewModelFixture.hpp"
#include "core/demo/DemoGenerator.hpp"
#include "core/domain/Player.hpp"
#include "services/DemoDataService.hpp"
#include "services/SquadService.hpp"
#include "viewmodels/CapabilityModel.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#include <gtest/gtest.h>

#include <chrono>

using namespace fdss::core;
using namespace fdss::tests;
using fdss::viewmodels::CapabilityModel;
using fdss::viewmodels::DashboardViewModel;

namespace {

using std::chrono::days;
using std::chrono::sys_days;

// FakeClock: today is Sunday 2026-11-01.
constexpr Date kToday{std::chrono::year{2026}, std::chrono::November, std::chrono::day{1}};

QString dashboardText(const char* source) {
    return QCoreApplication::translate("fdss::viewmodels::DashboardViewModel", source);
}

// The load signals come from the thread pool through the event loop.
bool waitForLoad(const DashboardViewModel& dashboard) {
    QElapsedTimer timer;
    timer.start();
    while (dashboard.busy() && timer.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
    }
    return !dashboard.busy();
}

QStringList column(const QVariantList& rows, const char* key) {
    QStringList values;
    for (const QVariant& row : rows) {
        values.append(row.toMap().value(QString::fromLatin1(key)).toString());
    }
    return values;
}

class DashboardViewModelTest : public ViewModelTest {
protected:
    void SetUp() override {
        ViewModelTest::SetUp();
        if (HasFatalFailure()) {
            return;
        }
        completeSetup();
        m_team = registry().setup().clubContext()->team.id;
    }

    void loadDemo() {
        const auto loaded = registry().demoData().load(m_team);
        ASSERT_TRUE(loaded.has_value()) << loaded.error().detail;
    }

    // Logs the Coach out and the given role in; needs the Coach logged in.
    void switchTo(Role role) {
        const StaffUserId id = addStaff("Inna Osoba", role);
        const auto check = registry().auth().login(id, kPin);
        ASSERT_TRUE(check.has_value()) << check.error().detail;
    }

    TeamId m_team;
};

} // namespace

TEST_F(DashboardViewModelTest, EmptyClubLocksSquadWithElevenPlayersToAdd) {
    CapabilityModel capabilities;
    capabilities.setTeamId(m_team.value);
    ASSERT_TRUE(capabilities.ready());
    // Event data is hidden until "Show advanced modules".
    EXPECT_EQ(capabilities.rowCount(), 5);
    const QVariantMap locks = capabilities.locks();
    EXPECT_TRUE(locks.contains(QStringLiteral("squad")));
    EXPECT_TRUE(locks.contains(QStringLiteral("matches")));
    EXPECT_TRUE(locks.contains(QStringLiteral("load")));
    EXPECT_FALSE(locks.contains(QStringLiteral("eventData")));

    const QVariantMap squad = capabilities.capability(QStringLiteral("squad"));
    EXPECT_EQ(squad.value(QStringLiteral("stateKey")).toString(), QStringLiteral("locked"));
    EXPECT_TRUE(squad.value(QStringLiteral("hint")).toString().contains(QStringLiteral("11")));
    EXPECT_EQ(squad.value(QStringLiteral("actionSection")).toString(), QStringLiteral("squad"));
    // The Coach may add players: a button, no request to someone else.
    EXPECT_FALSE(squad.value(QStringLiteral("actionText")).toString().isEmpty());
    EXPECT_TRUE(squad.value(QStringLiteral("askText")).toString().isEmpty());
    EXPECT_TRUE(capabilities.capability(QStringLiteral("load"))
                    .value(QStringLiteral("hint"))
                    .toString()
                    .contains(QStringLiteral("14")));

    capabilities.setShowAdvanced(true);
    EXPECT_EQ(capabilities.rowCount(), 6);
    EXPECT_TRUE(capabilities.locks().contains(QStringLiteral("eventData")));
}

TEST_F(DashboardViewModelTest, RoleWithoutPermissionIsToldWhoCanEnterTheData) {
    switchTo(Role::Analyst);
    CapabilityModel capabilities;
    capabilities.setTeamId(m_team.value);
    const QVariantMap squad = capabilities.capability(QStringLiteral("squad"));
    EXPECT_TRUE(squad.value(QStringLiteral("actionText")).toString().isEmpty());
    EXPECT_FALSE(squad.value(QStringLiteral("askText")).toString().isEmpty());

    DashboardViewModel dashboard;
    dashboard.setTeamId(m_team.value);
    ASSERT_TRUE(waitForLoad(dashboard));
    for (const QVariantMap& empty :
         {dashboard.nextMatchEmpty(), dashboard.todayEmpty(), dashboard.unavailableEmpty()}) {
        EXPECT_TRUE(empty.value(QStringLiteral("actionText")).toString().isEmpty());
        EXPECT_FALSE(empty.value(QStringLiteral("text")).toString().isEmpty());
    }
}

TEST_F(DashboardViewModelTest, CoachGetsAButtonInEveryEmptyCard) {
    DashboardViewModel dashboard;
    dashboard.setTeamId(m_team.value);
    ASSERT_TRUE(waitForLoad(dashboard));
    EXPECT_TRUE(dashboard.ready());
    EXPECT_FALSE(dashboard.hasNextMatch());
    EXPECT_TRUE(dashboard.daysText().isEmpty());
    EXPECT_TRUE(dashboard.unavailable().isEmpty());
    EXPECT_TRUE(dashboard.form().isEmpty());
    EXPECT_TRUE(dashboard.todaySessions().isEmpty());
    EXPECT_EQ(dashboard.nextMatchEmpty().value(QStringLiteral("actionSection")).toString(),
              QStringLiteral("calendar"));
    for (const QVariantMap& empty :
         {dashboard.nextMatchEmpty(), dashboard.todayEmpty(), dashboard.unavailableEmpty()}) {
        EXPECT_FALSE(empty.value(QStringLiteral("actionText")).toString().isEmpty());
    }
}

TEST_F(DashboardViewModelTest, DemoClubUnlocksEverythingButTheProModule) {
    loadDemo();
    CapabilityModel capabilities;
    capabilities.setTeamId(m_team.value);
    EXPECT_TRUE(capabilities.locks().isEmpty());
    EXPECT_EQ(capabilities.capability(QStringLiteral("eventData"))
                  .value(QStringLiteral("stateKey"))
                  .toString(),
              QStringLiteral("hidden"));
}

TEST_F(DashboardViewModelTest, DemoClubCards) {
    loadDemo();
    const demo::DemoDataset data =
        demo::generate(demo::kDemoSeed, Date{sys_days{kToday} - days{demo::kDemoDays}});
    const PlayerDraft& spike = data.players[static_cast<std::size_t>(data.story.loadSpike)].draft;
    const QString spikeName = QString::fromStdString(spike.firstName + " " + spike.lastName);

    DashboardViewModel dashboard;
    dashboard.setTeamId(m_team.value);
    ASSERT_TRUE(waitForLoad(dashboard));

    // The next match is on the Sunday of today.
    ASSERT_TRUE(dashboard.hasNextMatch());
    EXPECT_FALSE(dashboard.nextMatchLive());
    EXPECT_EQ(dashboard.nextMatchDate(), QStringLiteral("2026-11-01"));
    EXPECT_EQ(dashboard.daysToNextMatch(), 0);
    EXPECT_EQ(dashboard.daysText(), dashboardText("Today"));
    EXPECT_EQ(dashboard.nextMatchSection(), QStringLiteral("prematch"));
    EXPECT_FALSE(dashboard.nextMatchOpponent().isEmpty());
    EXPECT_FALSE(dashboard.lineupSaved());
    EXPECT_FALSE(dashboard.lineupReady());
    EXPECT_GT(dashboard.lineupIssueCount(), 0);
    EXPECT_FALSE(dashboard.lineupText().isEmpty());

    // Two injured and one suspended; the Coach reads the descriptions.
    const QStringList statuses = column(dashboard.unavailable(), "statusKey");
    EXPECT_EQ(statuses.count(QStringLiteral("injured")), 2);
    EXPECT_EQ(statuses.count(QStringLiteral("suspended")), 1);
    EXPECT_EQ(statuses.size(), 3);
    EXPECT_FALSE(column(dashboard.unavailable(), "description").join(QString{}).isEmpty());

    // The overloaded player is a signal to talk to them.
    const QVariantList signalRows = dashboard.loadSignals();
    const QStringList names = column(signalRows, "name");
    ASSERT_TRUE(names.contains(spikeName)) << names.join(QStringLiteral(", ")).toStdString();
    EXPECT_EQ(column(signalRows, "statusKey").first(), QStringLiteral("high"));
    EXPECT_TRUE(column(signalRows, "statusKey").count(QStringLiteral("high")) +
                    column(signalRows, "statusKey").count(QStringLiteral("elevated")) ==
                signalRows.size());

    // Last five finished matches, each with a result and a rating.
    const QVariantList form = dashboard.form();
    ASSERT_EQ(form.size(), 5);
    for (const QString& key : column(form, "resultKey")) {
        EXPECT_TRUE(key == QStringLiteral("win") || key == QStringLiteral("draw") ||
                    key == QStringLiteral("loss"))
            << key.toStdString();
    }
    EXPECT_FALSE(dashboard.formSummary().isEmpty());

    // Today's sessions of the demo, each opening its training sheet.
    ASSERT_FALSE(dashboard.todaySessions().isEmpty());
    for (const QVariant& row : dashboard.todaySessions()) {
        EXPECT_GT(row.toMap().value(QStringLiteral("sessionId")).toLongLong(), 0);
        EXPECT_FALSE(row.toMap().value(QStringLiteral("typeText")).toString().isEmpty());
    }
}

TEST_F(DashboardViewModelTest, AssistantDoesNotSeeAvailabilityDescriptions) {
    loadDemo();
    switchTo(Role::Assistant);
    DashboardViewModel dashboard;
    dashboard.setTeamId(m_team.value);
    ASSERT_TRUE(waitForLoad(dashboard));
    ASSERT_EQ(dashboard.unavailable().size(), 3);
    EXPECT_TRUE(column(dashboard.unavailable(), "description").join(QString{}).isEmpty());
}

TEST_F(DashboardViewModelTest, DaysToGoCountDownToTheMatch) {
    loadDemo();
    // Today moves back after the demo was loaded: its planned match is then 3 days ahead.
    m_clock->advance(-std::chrono::days{3});
    DashboardViewModel dashboard;
    dashboard.setTeamId(m_team.value);
    ASSERT_TRUE(waitForLoad(dashboard));
    ASSERT_TRUE(dashboard.hasNextMatch());
    EXPECT_EQ(dashboard.daysToNextMatch(), 3);
    EXPECT_EQ(dashboard.daysText(), dashboardText("In %1 days").arg(3));
}

TEST_F(DashboardViewModelTest, LoggedOutShowsNothing) {
    registry().auth().logout();
    DashboardViewModel dashboard;
    dashboard.setTeamId(m_team.value);
    EXPECT_FALSE(dashboard.ready());
    EXPECT_FALSE(dashboard.busy());
    CapabilityModel capabilities;
    capabilities.setTeamId(m_team.value);
    EXPECT_FALSE(capabilities.ready());
    EXPECT_EQ(capabilities.rowCount(), 0);
    EXPECT_TRUE(capabilities.locks().isEmpty());
}
