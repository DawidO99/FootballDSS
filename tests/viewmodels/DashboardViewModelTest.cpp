#include "viewmodels/DashboardViewModel.hpp"

#include "ViewModelFixture.hpp"
#include "core/demo/DemoGenerator.hpp"
#include "core/domain/Player.hpp"
#include "services/DemoDataService.hpp"
#include "services/SquadService.hpp"
#include "viewmodels/AppSettings.hpp"
#include "viewmodels/CapabilityModel.hpp"
#include "viewmodels/IsoDates.hpp"
#include "viewmodels/LoadTexts.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QScopeGuard>
#include <QSettings>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>

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

    bool sql(const QString& statement, const QVariantList& values = {}) {
        QSqlQuery query(m_db->database());
        if (!query.prepare(statement)) {
            ADD_FAILURE() << m_db->sqlError(query.lastError(), "prepare dashboard test").detail;
            return false;
        }
        for (qsizetype i = 0; i < values.size(); ++i) {
            query.bindValue(static_cast<int>(i), values[i]);
        }
        if (!query.exec()) {
            ADD_FAILURE() << m_db->sqlError(query.lastError(), "seed dashboard test").detail;
            return false;
        }
        return true;
    }

    void expectReadFailureClearsBindings(const QString& failingTable, bool loadOnly = false) {
        loadDemo();
        // A live match skips lineup attendance queries, leaving overview readable without
        // attendance.
        ASSERT_TRUE(sql(QStringLiteral("UPDATE match SET status = ? WHERE id = "
                                       "(SELECT id FROM match WHERE status = ? "
                                       "ORDER BY date, kickoff_time, id LIMIT 1)"),
                        {QStringLiteral("live"), QStringLiteral("planned")}));
        DashboardViewModel dashboard;
        dashboard.setTeamId(m_team.value);
        ASSERT_TRUE(waitForLoad(dashboard));
        ASSERT_FALSE(dashboard.loadSignals().isEmpty());
        const QVariantList absences = dashboard.unavailable();
        const QVariantList matches = dashboard.form();
        const QVariantList sessions = dashboard.todaySessions();
        const qint64 nextMatch = dashboard.nextMatchId();
        ASSERT_FALSE(absences.isEmpty());
        ASSERT_FALSE(matches.isEmpty());
        ASSERT_FALSE(sessions.isEmpty());
        ASSERT_NE(nextMatch, 0);

        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("dashboard"), &dashboard);
        QQmlComponent component(&engine);
        component.setData(R"(import QtQml
            QtObject {
                readonly property int rows: dashboard.loadSignals.length
                readonly property int absences: dashboard.unavailable.length
                readonly property int matches: dashboard.form.length
                readonly property int sessions: dashboard.todaySessions.length
                readonly property var nextMatch: dashboard.nextMatchId
                readonly property bool ready: dashboard.ready
                readonly property string error: dashboard.readError
                readonly property string loadError: dashboard.loadError
            })",
                          QUrl());
        const std::unique_ptr<QObject> binding(component.create());
        ASSERT_NE(binding, nullptr) << component.errorString().toStdString();
        ASSERT_GT(binding->property("rows").toInt(), 0);
        QString reported;
        QObject::connect(&dashboard, &DashboardViewModel::errorOccurred, &dashboard,
                         [&reported](const QString& error) { reported = error; });
        // Callers supply compile-time DDL only.
        ASSERT_TRUE(sql(failingTable));
        dashboard.reload();
        EXPECT_FALSE(dashboard.busy());
        EXPECT_EQ(binding->property("rows").toInt(), 0);
        EXPECT_FALSE(reported.isEmpty());
        if (loadOnly) {
            EXPECT_EQ(dashboard.unavailable(), absences);
            EXPECT_EQ(dashboard.form(), matches);
            EXPECT_EQ(dashboard.todaySessions(), sessions);
            EXPECT_EQ(binding->property("absences").toInt(), absences.size());
            EXPECT_EQ(binding->property("matches").toInt(), matches.size());
            EXPECT_EQ(binding->property("sessions").toInt(), sessions.size());
            EXPECT_EQ(binding->property("nextMatch").toLongLong(), nextMatch);
            EXPECT_TRUE(binding->property("ready").toBool());
            EXPECT_TRUE(binding->property("error").toString().isEmpty());
            EXPECT_EQ(binding->property("loadError").toString(), reported);
            ASSERT_TRUE(
                sql(QStringLiteral("ALTER TABLE attendance_unavailable RENAME TO attendance")));
            dashboard.reload();
            ASSERT_TRUE(waitForLoad(dashboard));
            EXPECT_GT(binding->property("rows").toInt(), 0);
            EXPECT_TRUE(binding->property("loadError").toString().isEmpty());
        } else {
            EXPECT_EQ(binding->property("absences").toInt(), 0);
            EXPECT_EQ(binding->property("matches").toInt(), 0);
            EXPECT_EQ(binding->property("sessions").toInt(), 0);
            EXPECT_FALSE(binding->property("ready").toBool());
            EXPECT_EQ(binding->property("error").toString(), reported);
            EXPECT_TRUE(binding->property("loadError").toString().isEmpty());
        }
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

TEST_F(DashboardViewModelTest, FourteenRpeDaysShowTheBaselineHintInsteadOfNoSignals) {
    ASSERT_TRUE(sql(QStringLiteral("INSERT INTO player (id, team_id, first_name, last_name, "
                                   "primary_position) VALUES (?, ?, ?, ?, ?)"),
                    {1, static_cast<qint64>(m_team.value), QStringLiteral("Jan"),
                     QStringLiteral("Gracz"), QStringLiteral("cm")}));
    for (int day = 0; day < 14; ++day) {
        const Date date{sys_days{kToday} - days{day}};
        ASSERT_TRUE(sql(QStringLiteral("INSERT INTO training_session (id, team_id, date, "
                                       "duration_min, type) VALUES (?, ?, ?, ?, ?)"),
                        {day + 1, static_cast<qint64>(m_team.value),
                         fdss::viewmodels::IsoDates::toIso(date), 60, QStringLiteral("training")}));
        ASSERT_TRUE(sql(QStringLiteral("INSERT INTO attendance (session_id, player_id, status, "
                                       "rpe) VALUES (?, ?, ?, ?)"),
                        {day + 1, 1, QStringLiteral("present"), 6}));
    }
    CapabilityModel capabilities;
    capabilities.setTeamId(m_team.value);
    ASSERT_EQ(capabilities.capability(QStringLiteral("load")).value(QStringLiteral("stateKey")),
              QStringLiteral("available"));

    DashboardViewModel dashboard;
    dashboard.setTeamId(m_team.value);
    ASSERT_TRUE(waitForLoad(dashboard));
    EXPECT_TRUE(dashboard.loadSignals().isEmpty());
    auto load = registry().load().teamLoad(m_team);
    ASSERT_TRUE(load.has_value());
    load->waitForFinished();
    const auto teamLoad = load->result();
    ASSERT_EQ(teamLoad.players.size(), 1U);
    const auto& assessment = teamLoad.players.front().assessment;
    ASSERT_EQ(assessment.verdict.status, LoadStatus::NotEnoughData);
    ASSERT_TRUE(assessment.baselineFrom.has_value());
    const QString hint = fdss::viewmodels::LoadTexts::hint(assessment);
    EXPECT_FALSE(hint.isEmpty());
    EXPECT_EQ(dashboard.loadSignalsEmpty().value(QStringLiteral("text")).toString(),
              dashboardText("Not enough data. %1").arg(hint));
}

TEST_F(DashboardViewModelTest, FailedLoadReadClearsRowsAndPublishesTheErrorToBindings) {
    expectReadFailureClearsBindings(
        QStringLiteral("ALTER TABLE attendance RENAME TO attendance_unavailable"), true);
}

TEST_F(DashboardViewModelTest, FailedOverviewReadClearsRowsAndPublishesTheErrorToBindings) {
    expectReadFailureClearsBindings(QStringLiteral("DROP TABLE availability"));
}

TEST_F(DashboardViewModelTest, MissingResultsStayUnknownInRowsAndTheGoalsSummary) {
    loadDemo();
    ASSERT_TRUE(sql(QStringLiteral("UPDATE match SET goals_for = NULL, goals_against = NULL")));
    ASSERT_TRUE(sql(QStringLiteral("UPDATE appearance SET rating = NULL")));
    DashboardViewModel dashboard;
    dashboard.setTeamId(m_team.value);
    ASSERT_TRUE(waitForLoad(dashboard));
    ASSERT_EQ(dashboard.form().size(), 5);
    for (const QVariant& item : dashboard.form()) {
        const QVariantMap row = item.toMap();
        EXPECT_EQ(row.value(QStringLiteral("resultKey")), QStringLiteral("none"));
        EXPECT_EQ(row.value(QStringLiteral("scoreText")), dashboardText("–"));
        EXPECT_EQ(row.value(QStringLiteral("resultText")), dashboardText("No result"));
    }
    EXPECT_EQ(dashboard.formSummary(), dashboardText("No results recorded"));
}

TEST_F(DashboardViewModelTest, GoalsSummaryCountsOnlyCompleteResults) {
    loadDemo();
    ASSERT_TRUE(sql(QStringLiteral("UPDATE match SET goals_for = NULL, goals_against = NULL")));
    ASSERT_TRUE(sql(QStringLiteral("UPDATE appearance SET rating = NULL")));
    DashboardViewModel dashboard;
    dashboard.setTeamId(m_team.value);
    ASSERT_TRUE(waitForLoad(dashboard));
    const QVariantList form = dashboard.form();
    ASSERT_EQ(form.size(), 5);
    const auto matchId = [&form](int row) {
        return form[row].toMap().value(QStringLiteral("matchId"));
    };
    ASSERT_TRUE(
        sql(QStringLiteral("UPDATE match SET goals_for = ?, goals_against = ? WHERE id = ?"),
            {2, 1, matchId(0)}));
    ASSERT_TRUE(
        sql(QStringLiteral("UPDATE match SET goals_for = ?, goals_against = ? WHERE id = ?"),
            {0, 0, matchId(1)}));
    // A one-sided result is still unknown, rather than 99:0.
    ASSERT_TRUE(
        sql(QStringLiteral("UPDATE match SET goals_for = ? WHERE id = ?"), {99, matchId(2)}));
    dashboard.reload();
    ASSERT_TRUE(waitForLoad(dashboard));
    EXPECT_EQ(column(dashboard.form(), "resultKey"),
              QStringList({QStringLiteral("win"), QStringLiteral("draw"), QStringLiteral("none"),
                           QStringLiteral("none"), QStringLiteral("none")}));
    EXPECT_EQ(dashboard.formSummary(),
              dashboardText("Goals: %1:%2 (matches with a result: %3 of %4)")
                  .arg(2)
                  .arg(1)
                  .arg(2)
                  .arg(5));
}

TEST_F(DashboardViewModelTest, LogoutDuringALoadReadDropsItsResult) {
    loadDemo();
    DashboardViewModel dashboard;
    registry().load().holdResults();
    const auto release = qScopeGuard([] { registry().load().releaseResults(); });
    dashboard.setTeamId(m_team.value);
    ASSERT_TRUE(dashboard.busy());
    registry().auth().logout();
    // Main.qml reloads the shared model on sessionSerialChanged.
    dashboard.reload();
    SignalCount changed(&dashboard, &DashboardViewModel::contentChanged);
    registry().load().releaseResults();
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
    EXPECT_EQ(changed.count(), 0);
    EXPECT_FALSE(dashboard.ready());
    EXPECT_FALSE(dashboard.busy());
    EXPECT_TRUE(dashboard.loadSignals().isEmpty());
    EXPECT_TRUE(dashboard.unavailable().isEmpty());
}

TEST_F(DashboardViewModelTest, AnOlderLoadResultAfterANewerReloadIsIgnored) {
    loadDemo();
    DashboardViewModel dashboard;
    registry().load().holdResults();
    const auto release = qScopeGuard([] { registry().load().releaseResults(); });
    dashboard.setTeamId(m_team.value);
    ASSERT_TRUE(dashboard.busy());
    // The older snapshot contains the load signals; the newer one has no RPE to assess.
    ASSERT_TRUE(sql(QStringLiteral("UPDATE attendance SET rpe = NULL")));
    ASSERT_TRUE(sql(QStringLiteral("UPDATE appearance SET rpe = NULL")));
    dashboard.reload();
    SignalCount changed(&dashboard, &DashboardViewModel::contentChanged);
    registry().load().releaseResults();
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
    EXPECT_EQ(changed.count(), 1);
    EXPECT_FALSE(dashboard.busy());
    EXPECT_TRUE(dashboard.ready());
    EXPECT_TRUE(dashboard.loadSignals().isEmpty());
}

TEST(AppSettingsPersistenceTest, AdvancedModulesSurvivesNewInstances) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto previousFormat = QSettings::defaultFormat();
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    const auto restore =
        qScopeGuard([previousFormat] { QSettings::setDefaultFormat(previousFormat); });
    {
        fdss::viewmodels::AppSettings settings;
        EXPECT_FALSE(settings.showAdvancedModules());
        settings.setShowAdvancedModules(true);
    }
    {
        fdss::viewmodels::AppSettings settings;
        EXPECT_TRUE(settings.showAdvancedModules());
        settings.setShowAdvancedModules(false);
    }
    EXPECT_FALSE(fdss::viewmodels::AppSettings().showAdvancedModules());
}
