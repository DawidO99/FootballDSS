#include "viewmodels/LoadViewModel.hpp"

#include "ViewModelFixture.hpp"
#include "core/demo/DemoGenerator.hpp"
#include "core/domain/Player.hpp"
#include "core/domain/TrainingSession.hpp"
#include "services/CalendarService.hpp"
#include "services/DemoDataService.hpp"
#include "services/Session.hpp"
#include "services/SquadService.hpp"
#include "services/TrainingService.hpp"
#include "viewmodels/PlayerLoadViewModel.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QStringList>
#include <QThread>

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <vector>

using namespace fdss::core;
using namespace fdss::tests;
using fdss::viewmodels::LoadViewModel;
using fdss::viewmodels::PlayerLoadViewModel;

namespace {

using std::chrono::days;
using std::chrono::sys_days;

// FakeClock: today is Sunday 2026-11-01.
constexpr Date kToday{std::chrono::year{2026}, std::chrono::November, std::chrono::day{1}};

Date daysAgo(int count) { return Date{sys_days{kToday} - days{count}}; }

// The result comes from the thread pool through the event loop.
template <class ViewModel> bool waitForResult(const ViewModel& viewModel) {
    QElapsedTimer timer;
    timer.start();
    while (viewModel.busy() && timer.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
    }
    return !viewModel.busy();
}

class LoadViewModelTest : public ViewModelTest {
protected:
    void SetUp() override {
        ViewModelTest::SetUp();
        if (HasFatalFailure()) {
            return;
        }
        completeSetup();
        m_team = registry().setup().clubContext()->team.id;
    }

    PlayerId addPlayer(const char* firstName, const char* lastName, int number) {
        PlayerDraft draft;
        draft.firstName = firstName;
        draft.lastName = lastName;
        draft.shirtNumber = number;
        draft.primaryPosition = Position::Cm;
        const auto id = registry().squad().addPlayer(m_team, draft);
        EXPECT_TRUE(id.has_value()) << id.error().detail;
        return id.value_or(PlayerId{});
    }

    // A session with RPE for the given players.
    void addSession(Date date, int duration, const std::vector<std::pair<PlayerId, int>>& rpe) {
        TrainingSessionDraft draft;
        draft.date = date;
        draft.durationMin = duration;
        const auto id = registry().calendar().addSession(m_team, draft);
        ASSERT_TRUE(id.has_value()) << id.error().detail;
        std::vector<AttendanceDraft> rows;
        for (const auto& [player, value] : rpe) {
            AttendanceDraft row;
            row.playerId = player;
            row.rpe = value;
            rows.push_back(row);
        }
        const auto saved = registry().training().saveAttendance(*id, rows);
        ASSERT_TRUE(saved.has_value()) << saved.error().detail;
        ASSERT_TRUE(saved->empty());
    }

    // Kowalski 7: 20 sessions over 40 days (a status); Nowak 9: 3 sessions (grey); Żak 4: nothing.
    void seedMixed() {
        m_kowalski = addPlayer("Jan", "Kowalski", 7);
        m_nowak = addPlayer("Adam", "Nowak", 9);
        m_zak = addPlayer("Piotr", "Żak", 4);
        for (int ago = 40; ago >= 2; ago -= 2) {
            std::vector<std::pair<PlayerId, int>> rpe{{m_kowalski, 5}};
            if (ago <= 6) {
                rpe.emplace_back(m_nowak, 6);
            }
            addSession(daysAgo(ago), 60, rpe);
        }
    }

    void open(LoadViewModel& load) const {
        load.setTeamId(m_team.value);
        ASSERT_TRUE(waitForResult(load));
    }

    [[nodiscard]] static QStringList column(const LoadViewModel& load, const char* role) {
        QStringList values;
        for (int row = 0; row < load.players()->rowCount(); ++row) {
            values.append(load.rowAt(row).value(QString::fromLatin1(role)).toString());
        }
        return values;
    }

    TeamId m_team;
    PlayerId m_kowalski;
    PlayerId m_nowak;
    PlayerId m_zak;
};

} // namespace

TEST_F(LoadViewModelTest, EmptyWithoutATeamOrALogin) {
    LoadViewModel load;
    EXPECT_EQ(load.playerCount(), 0);
    EXPECT_FALSE(load.busy());

    addPlayer("Jan", "Kowalski", 7);
    registry().session().clear();
    load.setTeamId(m_team.value);
    EXPECT_FALSE(load.busy());
    EXPECT_EQ(load.playerCount(), 0);
    EXPECT_TRUE(load.loadError().isEmpty());
}

TEST_F(LoadViewModelTest, BusyUntilTheResultIsIn) {
    seedMixed();
    LoadViewModel load;
    SignalCount reloaded(&load, &LoadViewModel::playersReloaded);
    load.setTeamId(m_team.value);
    EXPECT_TRUE(load.busy());
    ASSERT_TRUE(waitForResult(load));
    EXPECT_EQ(reloaded.count(), 1);
    EXPECT_EQ(load.playerCount(), 3);
    EXPECT_EQ(load.asOfText(), QStringLiteral("As of 31/10/2026"));
}

TEST_F(LoadViewModelTest, AResultOfAnOlderRequestIsDropped) {
    seedMixed();
    LoadViewModel load;
    load.setTeamId(m_team.value);
    load.setTeamId(0);
    EXPECT_FALSE(load.busy());
    // Let the first computation finish and reach the event loop.
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 200) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    EXPECT_EQ(load.playerCount(), 0);
}

TEST_F(LoadViewModelTest, GreyRowsShowTheAcuteLoadOnlyAndSayWhatIsMissing) {
    seedMixed();
    LoadViewModel load;
    open(load);

    const int nowak = load.rowOf(m_nowak.value);
    ASSERT_GE(nowak, 0);
    const QVariantMap row = load.rowAt(nowak);
    EXPECT_EQ(row.value(QStringLiteral("statusKey")), QStringLiteral("notEnoughData"));
    EXPECT_EQ(row.value(QStringLiteral("statusText")), QStringLiteral("Not enough data"));
    EXPECT_FALSE(row.value(QStringLiteral("acuteText")).toString().isEmpty());
    EXPECT_TRUE(row.value(QStringLiteral("chronicText")).toString().isEmpty());
    EXPECT_TRUE(row.value(QStringLiteral("ratioText")).toString().isEmpty());
    EXPECT_FALSE(row.value(QStringLiteral("ratio")).isValid());
    EXPECT_TRUE(row.value(QStringLiteral("hint"))
                    .toString()
                    .startsWith(QStringLiteral("Days with RPE still needed: 11.")));

    const QVariantMap zak = load.rowAt(load.rowOf(m_zak.value));
    EXPECT_TRUE(zak.value(QStringLiteral("acuteText")).toString().isEmpty());
    EXPECT_EQ(zak.value(QStringLiteral("hint")), QStringLiteral("Days with RPE still needed: 14."));

    const QVariantMap kowalski = load.rowAt(load.rowOf(m_kowalski.value));
    EXPECT_NE(kowalski.value(QStringLiteral("statusKey")), QStringLiteral("notEnoughData"));
    EXPECT_FALSE(kowalski.value(QStringLiteral("ratioText")).toString().isEmpty());
    // Somebody has a status, so the team needs no hint.
    EXPECT_TRUE(load.teamHint().isEmpty());
}

TEST_F(LoadViewModelTest, ANewClubWithThreeDaysOfRpeIsGreyWithAHint) {
    const PlayerId first = addPlayer("Jan", "Kowalski", 7);
    const PlayerId second = addPlayer("Adam", "Nowak", 9);
    for (const int ago : {5, 3, 1}) {
        addSession(daysAgo(ago), 90, {{first, 5}, {second, 7}});
    }
    LoadViewModel load;
    open(load);

    EXPECT_EQ(column(load, "statusKey"),
              QStringList({QStringLiteral("notEnoughData"), QStringLiteral("notEnoughData")}));
    // No green, and the team is told what to enter.
    EXPECT_EQ(load.teamHint(),
              QStringLiteral("Days with RPE still needed: 11. Full comparison from 24/11/2026."));
}

TEST_F(LoadViewModelTest, SortsByEveryColumnWithEmptyValuesLast) {
    seedMixed();
    LoadViewModel load;
    open(load);

    // By status: the one with a status first, then grey (in the order of the source).
    EXPECT_EQ(load.sortRole(), QStringLiteral("statusKey"));
    EXPECT_EQ(column(load, "name").first(), QStringLiteral("Jan Kowalski"));

    load.sortBy(QStringLiteral("acuteText"), true);
    EXPECT_EQ(column(load, "name").last(), QStringLiteral("Piotr Żak"));
    load.sortBy(QStringLiteral("acuteText"), false);
    EXPECT_EQ(column(load, "name").last(), QStringLiteral("Piotr Żak"));

    load.sortBy(QStringLiteral("ratioText"), false);
    EXPECT_EQ(column(load, "name").first(), QStringLiteral("Jan Kowalski"));
    EXPECT_EQ(load.sortRole(), QStringLiteral("ratioText"));
    EXPECT_FALSE(load.sortAscending());

    load.sortBy(QStringLiteral("name"), true);
    EXPECT_EQ(column(load, "name"),
              QStringList({QStringLiteral("Jan Kowalski"), QStringLiteral("Adam Nowak"),
                           QStringLiteral("Piotr Żak")}));

    load.sortBy(QStringLiteral("number"), true);
    EXPECT_EQ(column(load, "number"),
              QStringList({QStringLiteral("4"), QStringLiteral("7"), QStringLiteral("9")}));

    load.sortBy(QStringLiteral("nothing"), true);
    EXPECT_EQ(load.sortRole(), QStringLiteral("number"));
}

TEST_F(LoadViewModelTest, FiltersByStatus) {
    seedMixed();
    LoadViewModel load;
    open(load);
    SignalCount filters(&load, &LoadViewModel::filtersChanged);

    load.setStatusFilter(QStringLiteral("notEnoughData"));
    EXPECT_TRUE(load.filtersActive());
    EXPECT_EQ(load.players()->rowCount(), 2);
    EXPECT_EQ(load.rowOf(m_kowalski.value), -1);
    EXPECT_EQ(load.playerCount(), 3);

    load.setStatusFilter(QStringLiteral("high"));
    EXPECT_EQ(load.players()->rowCount(), 0);

    load.setStatusFilter(QStringLiteral("unknown"));
    EXPECT_EQ(load.statusFilter(), QStringLiteral("high"));

    load.clearFilters();
    EXPECT_FALSE(load.filtersActive());
    EXPECT_EQ(load.players()->rowCount(), 3);
    EXPECT_EQ(filters.count(), 3);

    const QVariantList options = load.statusFilterOptions();
    ASSERT_EQ(options.size(), 6);
    EXPECT_EQ(options.front().toMap().value(QStringLiteral("text")), QStringLiteral("All (3)"));
    EXPECT_EQ(options.back().toMap().value(QStringLiteral("value")),
              QStringLiteral("notEnoughData"));
    EXPECT_EQ(options.back().toMap().value(QStringLiteral("text")),
              QStringLiteral("Not enough data (2)"));
}

TEST_F(LoadViewModelTest, TheDemoClubShowsTheLoadSpikeAsASignalToTalk) {
    ASSERT_TRUE(registry().demoData().load(m_team).has_value());
    const demo::DemoDataset data = demo::generate(demo::kDemoSeed, daysAgo(demo::kDemoDays));
    const PlayerDraft& spike = data.players[static_cast<std::size_t>(data.story.loadSpike)].draft;
    const QString spikeName = QString::fromStdString(spike.firstName + " " + spike.lastName);

    LoadViewModel load;
    open(load);
    ASSERT_EQ(load.playerCount(), demo::kActivePlayers);
    // Sorted by status: the most urgent first.
    const QStringList keys = column(load, "statusKey");
    const QStringList names = column(load, "name");
    const auto spikeRow = names.indexOf(spikeName);
    ASSERT_GE(spikeRow, 0);
    EXPECT_EQ(keys[spikeRow], QStringLiteral("high"));
    EXPECT_EQ(keys.first(), QStringLiteral("high"));
    EXPECT_FALSE(keys.contains(QStringLiteral("notEnoughData")));

    const QString explanation =
        load.rowAt(static_cast<int>(spikeRow)).value(QStringLiteral("explanation")).toString();
    EXPECT_TRUE(explanation.endsWith(QStringLiteral("A signal to talk to the player.")))
        << explanation.toStdString();

    // SPEC 7.1: never "injury risk", in any text of the screen.
    for (int row = 0; row < load.players()->rowCount(); ++row) {
        const QVariantMap values = load.rowAt(row);
        for (const char* role : {"statusText", "explanation", "hint"}) {
            const QString text = values.value(QString::fromLatin1(role)).toString();
            EXPECT_FALSE(text.contains(QStringLiteral("injur"), Qt::CaseInsensitive)) << role;
            EXPECT_FALSE(text.contains(QStringLiteral("risk"), Qt::CaseInsensitive)) << role;
        }
    }
}

TEST_F(LoadViewModelTest, PlayerLoadOfANewPlayerIsGreyWithAcuteOnly) {
    const PlayerId player = addPlayer("Jan", "Kowalski", 7);
    for (const int ago : {3, 2, 1}) {
        addSession(daysAgo(ago), 60, {{player, 5}});
    }
    PlayerLoadViewModel load;
    load.setPlayerId(player.value);
    EXPECT_TRUE(load.busy());
    ASSERT_TRUE(waitForResult(load));

    EXPECT_TRUE(load.hasData());
    EXPECT_FALSE(load.baselineReady());
    EXPECT_EQ(load.statusKey(), QStringLiteral("notEnoughData"));
    EXPECT_EQ(load.hint(),
              QStringLiteral("Days with RPE still needed: 11. Full comparison from 26/11/2026."));
    EXPECT_FALSE(load.acuteText().isEmpty());
    EXPECT_TRUE(load.chronicText().isEmpty());
    EXPECT_TRUE(load.ratioText().isEmpty());
    EXPECT_EQ(load.weekLoadText(), QStringLiteral("900"));
    EXPECT_EQ(load.asOfText(), QStringLiteral("As of 31/10/2026"));

    // 56 days through today; the sessions on days 52..54, today without data.
    ASSERT_EQ(load.sessionLoads().size(), PlayerLoadViewModel::kChartDays);
    ASSERT_EQ(load.chartDayLabels().size(), PlayerLoadViewModel::kChartDays);
    EXPECT_EQ(load.chartDayLabels().last(), QStringLiteral("11/01"));
    EXPECT_EQ(load.sessionLoads()[52], 300.0);
    EXPECT_EQ(load.sessionLoads()[51], 0.0);
    EXPECT_EQ(load.acutePoints().size(), 3);
    EXPECT_EQ(load.acutePoints().front().x(), 52.0);
    EXPECT_TRUE(load.chronicPoints().isEmpty());
    EXPECT_TRUE(load.ratioPoints().isEmpty());
    EXPECT_EQ(load.loadAxisMax(), 300.0);
    EXPECT_EQ(load.loadAxisStep(), 100.0);
    EXPECT_EQ(load.ratioAxisMax(), 2.0);

    const QVariantMap today = load.dayAt(55);
    EXPECT_EQ(today.value(QStringLiteral("title")), QStringLiteral("01/11/2026"));
    const QVariantList noData = today.value(QStringLiteral("rows")).toList();
    ASSERT_EQ(noData.size(), 1);
    EXPECT_EQ(noData.front().toMap().value(QStringLiteral("key")), QStringLiteral("none"));
    const QVariantList session = load.dayAt(52).value(QStringLiteral("rows")).toList();
    ASSERT_EQ(session.size(), 3); // training, match, acute
    EXPECT_EQ(session.front().toMap().value(QStringLiteral("value")), QStringLiteral("300 AU"));
    EXPECT_TRUE(load.dayAt(56).isEmpty());
}

TEST_F(LoadViewModelTest, PlayerLoadOfTheDemoSpikeExplainsTheChart) {
    ASSERT_TRUE(registry().demoData().load(m_team).has_value());
    const demo::DemoDataset data = demo::generate(demo::kDemoSeed, daysAgo(demo::kDemoDays));
    LoadViewModel team;
    open(team);
    const PlayerDraft& spike = data.players[static_cast<std::size_t>(data.story.loadSpike)].draft;
    const QString spikeName = QString::fromStdString(spike.firstName + " " + spike.lastName);
    const int row = static_cast<int>(column(team, "name").indexOf(spikeName));
    ASSERT_GE(row, 0);

    PlayerLoadViewModel load;
    load.setPlayerId(team.rowAt(row).value(QStringLiteral("playerId")).toLongLong());
    ASSERT_TRUE(waitForResult(load));

    EXPECT_EQ(load.statusKey(), QStringLiteral("high"));
    EXPECT_TRUE(load.baselineReady());
    EXPECT_FALSE(load.ratioText().isEmpty());
    EXPECT_FALSE(load.monotonyText().isEmpty());
    EXPECT_FALSE(load.strainText().isEmpty());
    EXPECT_TRUE(load.hint().isEmpty());
    // The last match (Sunday a week ago, index 48) in full at RPE 9.
    EXPECT_EQ(load.matchLoads()[48], 810.0);
    EXPECT_FALSE(load.chronicPoints().isEmpty());
    EXPECT_FALSE(load.ratioPoints().isEmpty());
    EXPECT_GT(load.ratioPoints().last().y(), 1.5);
    EXPECT_EQ(load.ratioPoints().last().x(), 54.0);
    EXPECT_GE(load.ratioAxisMax(), 2.0);
    const QVariantMap thresholds = load.thresholds();
    EXPECT_EQ(thresholds.value(QStringLiteral("high")).toDouble(), 1.5);
    EXPECT_EQ(thresholds.value(QStringLiteral("highText")), QStringLiteral("1.5"));

    const QVariantList rows = load.dayAt(54).value(QStringLiteral("rows")).toList();
    QStringList keys;
    for (const QVariant& entry : rows) {
        keys.append(entry.toMap().value(QStringLiteral("key")).toString());
    }
    EXPECT_EQ(keys, QStringList({QStringLiteral("sessions"), QStringLiteral("matches"),
                                 QStringLiteral("acute"), QStringLiteral("chronic"),
                                 QStringLiteral("ratio")}));
}

TEST_F(LoadViewModelTest, PlayerLoadReportsAReadError) {
    PlayerLoadViewModel load;
    load.setPlayerId(999);
    EXPECT_FALSE(load.busy());
    EXPECT_FALSE(load.loadError().isEmpty());
    EXPECT_FALSE(load.hasData());
    EXPECT_EQ(load.sessionLoads().size(), PlayerLoadViewModel::kChartDays);
}

TEST_F(LoadViewModelTest, PlayerLoadNamesTheMissingRpe) {
    const PlayerId player = addPlayer("Jan", "Kowalski", 7);
    addSession(daysAgo(5), 60, {{player, 5}});
    // Present, RPE not entered yet.
    TrainingSessionDraft draft;
    draft.date = daysAgo(3);
    const auto id = registry().calendar().addSession(m_team, draft);
    ASSERT_TRUE(id.has_value());
    AttendanceDraft present;
    present.playerId = player;
    ASSERT_TRUE(registry().training().saveAttendance(*id, std::vector{present}).has_value());

    PlayerLoadViewModel load;
    load.setPlayerId(player.value);
    ASSERT_TRUE(waitForResult(load));
    EXPECT_EQ(load.missingRpeText(),
              QStringLiteral("Without RPE in the last 28 days: sessions 1, matches 0."));
}
