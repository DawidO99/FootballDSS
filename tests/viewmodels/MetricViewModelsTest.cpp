#include "ViewModelFixture.hpp"
#include "core/domain/Match.hpp"
#include "core/domain/Opponent.hpp"
#include "core/domain/Player.hpp"
#include "core/domain/TrainingSession.hpp"
#include "services/AuthService.hpp"
#include "services/CalendarService.hpp"
#include "services/MetricService.hpp"
#include "services/Session.hpp"
#include "services/SquadService.hpp"
#include "viewmodels/MetricDefinitionsViewModel.hpp"
#include "viewmodels/MetricEntryViewModel.hpp"
#include "viewmodels/MetricTexts.hpp"
#include "viewmodels/PlayerMetricsViewModel.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <optional>
#include <vector>

using namespace fdss::core;
using namespace fdss::tests;
using fdss::viewmodels::MetricDefinitionListModel;
using fdss::viewmodels::MetricDefinitionsViewModel;
using fdss::viewmodels::MetricEntryViewModel;
using fdss::viewmodels::MetricTexts;
using fdss::viewmodels::PlayerMetricsViewModel;
using namespace std::chrono;

namespace {

// FakeClock says today is 2026-11-01.
constexpr Date kToday = 2026y / November / 1d;
constexpr Date kLastWeek = 2026y / October / 25d;

QVariantMap sprintForm() {
    return {{QStringLiteral("namePl"), QStringLiteral("Sprint 30 m")},
            {QStringLiteral("nameEn"), QStringLiteral("Sprint 30 m")},
            {QStringLiteral("valueType"), QStringLiteral("decimal")},
            {QStringLiteral("unit"), QStringLiteral("s")},
            {QStringLiteral("minText"), QStringLiteral("3")},
            {QStringLiteral("maxText"), QStringLiteral("10,5")},
            {QStringLiteral("aggregation"), QStringLiteral("avg")},
            {QStringLiteral("scope"), QStringLiteral("training")},
            {QStringLiteral("higherIsBetter"), false}};
}

class MetricViewModelsTest : public ViewModelTest {
protected:
    void SetUp() override {
        ViewModelTest::SetUp();
        if (HasFatalFailure()) {
            return;
        }
        completeSetup();
        m_team = registry().setup().clubContext()->team.id;
    }

    TrainingSessionId addSession(Date date) {
        TrainingSessionDraft draft;
        draft.date = date;
        const auto id = registry().calendar().addSession(m_team, draft);
        EXPECT_TRUE(id.has_value()) << id.error().detail;
        return id.value_or(TrainingSessionId{});
    }

    MatchId addMatch(Date date) {
        if (m_opponent.value == 0) {
            OpponentDraft opponent;
            opponent.name = "Rywal";
            const auto opponentId = registry().calendar().addOpponent(opponent);
            EXPECT_TRUE(opponentId.has_value());
            m_opponent = opponentId.value_or(OpponentId{});
        }
        MatchDraft draft;
        draft.opponentId = m_opponent;
        draft.date = date;
        const auto id = registry().calendar().addMatch(m_team, draft);
        EXPECT_TRUE(id.has_value()) << id.error().detail;
        return id.value_or(MatchId{});
    }

    PlayerId addPlayer(const char* first, const char* last, int number) {
        PlayerDraft draft;
        draft.firstName = first;
        draft.lastName = last;
        draft.shirtNumber = number;
        const auto id = registry().squad().addPlayer(m_team, draft);
        EXPECT_TRUE(id.has_value()) << id.error().detail;
        return id.value_or(PlayerId{});
    }

    void logInAs(Role role) {
        const StaffUserId id = addStaff("Inny", role);
        const auto check = registry().auth().login(id, kPin);
        ASSERT_TRUE(check.has_value());
        ASSERT_EQ(check->status, fdss::services::PinCheckStatus::Accepted);
    }

    MetricDefinitionId addSprint() {
        MetricDefinitionDraft draft;
        draft.nameEn = "Sprint 30 m";
        draft.unit = "s";
        draft.min = 3.0;
        draft.max = 10.0;
        draft.higherIsBetter = false;
        const auto id = registry().metrics().addDefinition(draft);
        EXPECT_TRUE(id.has_value()) << id.error().detail;
        return id.value_or(MetricDefinitionId{});
    }

    std::unique_ptr<MetricEntryViewModel> makeEntry() {
        auto entry = std::make_unique<MetricEntryViewModel>();
        entry->setTeamId(m_team.value);
        return entry;
    }

    static QVariant value(const MetricEntryViewModel& entry, int row,
                          MetricEntryViewModel::DataRole role) {
        return entry.index(row).data(role);
    }

    TeamId m_team;
    OpponentId m_opponent;
};

TEST_F(MetricViewModelsTest, DefinitionIsAddedFromTheForm) {
    MetricDefinitionsViewModel definitions;
    EXPECT_TRUE(definitions.canManage());
    const SignalCount changed(&definitions, &MetricDefinitionsViewModel::definitionsChanged);
    ASSERT_TRUE(definitions.add(sprintForm()));
    EXPECT_EQ(changed.count(), 1);
    ASSERT_EQ(definitions.definitions()->count(), 1);
    const QVariantMap row = definitions.definitions()->rowAt(0);
    EXPECT_EQ(row.value(QStringLiteral("key")).toString(), QStringLiteral("sprint_30_m"));
    EXPECT_EQ(row.value(QStringLiteral("scope")).toString(), QStringLiteral("training"));
    EXPECT_EQ(row.value(QStringLiteral("valueCount")).toInt(), 0);
    const auto stored = registry().metrics().listDefinitions();
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->front().definition.max, 10.5) << "decimal comma in the form";
    EXPECT_FALSE(stored->front().definition.higherIsBetter);
}

TEST_F(MetricViewModelsTest, FormErrorsAreKeyedByField) {
    MetricDefinitionsViewModel definitions;
    QVariantMap form = sprintForm();
    form.insert(QStringLiteral("namePl"), QString{});
    form.insert(QStringLiteral("nameEn"), QString{});
    form.insert(QStringLiteral("minText"), QStringLiteral("abc"));
    form.insert(QStringLiteral("maxText"), QStringLiteral("2"));
    EXPECT_FALSE(definitions.add(form));
    const QVariantMap errors = definitions.fieldErrors();
    EXPECT_TRUE(errors.contains(QStringLiteral("namePl")));
    EXPECT_TRUE(errors.contains(QStringLiteral("minText")));
    EXPECT_EQ(definitions.definitions()->count(), 0);

    ASSERT_TRUE(definitions.add(sprintForm()));
    EXPECT_TRUE(definitions.fieldErrors().isEmpty());
    QVariantMap similar = sprintForm();
    similar.insert(QStringLiteral("nameEn"), QStringLiteral("sprint-30-M"));
    EXPECT_FALSE(definitions.add(similar)) << "the key is taken";
    EXPECT_TRUE(definitions.fieldErrors().contains(QStringLiteral("nameEn")));
}

TEST_F(MetricViewModelsTest, BooleanFormOffersOnlyItsAggregations) {
    MetricDefinitionsViewModel definitions;
    EXPECT_EQ(definitions.aggregationOptions(QStringLiteral("boolean")).size(), 3);
    EXPECT_EQ(definitions.aggregationOptions(QStringLiteral("decimal")).size(), 5);
    QVariantMap form = sprintForm();
    form.insert(QStringLiteral("valueType"), QStringLiteral("boolean"));
    form.insert(QStringLiteral("aggregation"), QStringLiteral("max"));
    EXPECT_FALSE(definitions.add(form));
    EXPECT_TRUE(definitions.fieldErrors().contains(QStringLiteral("minText")));
    EXPECT_TRUE(definitions.fieldErrors().contains(QStringLiteral("aggregation")));
}

TEST_F(MetricViewModelsTest, DeleteIsUndoneAndRefusedWithValues) {
    MetricDefinitionsViewModel definitions;
    ASSERT_TRUE(definitions.add(sprintForm()));
    const qint64 id =
        definitions.definitions()->rowAt(0).value(QStringLiteral("metricId")).toLongLong();
    const SignalCount removed(&definitions, &MetricDefinitionsViewModel::removed);
    ASSERT_TRUE(definitions.remove(id));
    EXPECT_EQ(removed.count(), 1);
    EXPECT_EQ(definitions.definitions()->count(), 0);
    ASSERT_TRUE(definitions.canUndo());
    ASSERT_TRUE(definitions.undoRemove());
    ASSERT_EQ(definitions.definitions()->count(), 1);
    EXPECT_EQ(definitions.definitions()->rowAt(0).value(QStringLiteral("key")).toString(),
              QStringLiteral("sprint_30_m"));

    const qint64 restored =
        definitions.definitions()->rowAt(0).value(QStringLiteral("metricId")).toLongLong();
    const PlayerId player = addPlayer("Jan", "Kowalski", 7);
    const TrainingSessionId session = addSession(kToday);
    const std::vector<MetricValueDraft> values{{player, 4.3}};
    ASSERT_TRUE(registry()
                    .metrics()
                    .saveValues(MetricDefinitionId{restored}, m_team, session, values)
                    .has_value());
    definitions.reload();
    EXPECT_TRUE(definitions.isLocked(restored));
    const SignalCount errors(&definitions, &MetricDefinitionsViewModel::errorOccurred);
    EXPECT_FALSE(definitions.remove(restored));
    EXPECT_EQ(errors.count(), 1) << "the toast explains why";
    EXPECT_EQ(definitions.definitions()->count(), 1);
}

TEST_F(MetricViewModelsTest, PhysioReadsButCannotChange) {
    addSprint();
    addPlayer("Jan", "Kowalski", 7);
    addSession(kToday);
    logInAs(Role::Physio);
    MetricDefinitionsViewModel definitions;
    EXPECT_FALSE(definitions.canManage());
    EXPECT_EQ(definitions.definitions()->count(), 1) << "everyone reads";
    const SignalCount errors(&definitions, &MetricDefinitionsViewModel::errorOccurred);
    EXPECT_FALSE(definitions.add(sprintForm()));
    EXPECT_EQ(errors.count(), 1);

    auto entry = makeEntry();
    EXPECT_TRUE(entry->readOnly());
    ASSERT_EQ(entry->count(), 1);
    entry->setValueText(0, QStringLiteral("4,3"));
    EXPECT_FALSE(entry->dirty()) << "a read-only cell does not change";
    EXPECT_FALSE(entry->save());
}

TEST_F(MetricViewModelsTest, AnalystManagesMetrics) {
    logInAs(Role::Analyst);
    MetricDefinitionsViewModel definitions;
    EXPECT_TRUE(definitions.canManage());
    EXPECT_TRUE(definitions.add(sprintForm()));
}

TEST_F(MetricViewModelsTest, EntryOpensTheLatestSessionAndSavesDecimalComma) {
    const MetricDefinitionId metric = addSprint();
    const PlayerId first = addPlayer("Jan", "Kowalski", 7);
    addPlayer("Adam", "Nowak", 9);
    addSession(kLastWeek);
    const TrainingSessionId latest = addSession(kToday);

    auto entry = makeEntry();
    EXPECT_EQ(entry->metricId(), metric.value);
    EXPECT_EQ(entry->scope(), QStringLiteral("training"));
    EXPECT_EQ(entry->targetId(), latest.value) << "the most recent session on or before today";
    ASSERT_EQ(entry->count(), 2);
    EXPECT_FALSE(entry->dirty());
    EXPECT_FALSE(entry->canSave());

    entry->setValueText(0, QStringLiteral("4,35"));
    EXPECT_TRUE(entry->dirty());
    EXPECT_TRUE(entry->canSave());
    EXPECT_EQ(value(*entry, 0, MetricEntryViewModel::ValueTextRole).toString(),
              QStringLiteral("4,35"))
        << "the text stays as typed";
    EXPECT_FALSE(value(*entry, 0, MetricEntryViewModel::HasErrorRole).toBool());

    const SignalCount saved(entry.get(), &MetricEntryViewModel::saved);
    ASSERT_TRUE(entry->save());
    EXPECT_EQ(saved.count(), 1);
    EXPECT_FALSE(entry->dirty());
    const auto stored = registry().metrics().sheet(metric, m_team, latest);
    ASSERT_TRUE(stored.has_value());
    const auto row = std::ranges::find_if(
        stored->rows, [&](const auto& sheetRow) { return sheetRow.player.id == first; });
    ASSERT_NE(row, stored->rows.end());
    EXPECT_EQ(row->value, 4.35);
    EXPECT_EQ(value(*entry, 0, MetricEntryViewModel::ValueTextRole).toString(),
              MetricTexts::number(4.35));
}

TEST_F(MetricViewModelsTest, WrongValuesStayInTheRowAndBlockTheSave) {
    addSprint();
    addPlayer("Jan", "Kowalski", 7);
    addPlayer("Adam", "Nowak", 9);
    addSession(kToday);
    auto entry = makeEntry();

    entry->setValueText(0, QStringLiteral("abc"));
    entry->setValueText(1, QStringLiteral("12"));
    EXPECT_TRUE(value(*entry, 0, MetricEntryViewModel::HasErrorRole).toBool());
    EXPECT_FALSE(value(*entry, 0, MetricEntryViewModel::RowErrorRole).toString().isEmpty());
    EXPECT_TRUE(value(*entry, 1, MetricEntryViewModel::HasErrorRole).toBool()) << "above max";
    EXPECT_EQ(entry->firstErrorRow(), 0);
    const SignalCount errors(entry.get(), &MetricEntryViewModel::errorOccurred);
    EXPECT_FALSE(entry->save());
    EXPECT_EQ(errors.count(), 1);
    EXPECT_TRUE(entry->dirty());

    entry->setValueText(0, QString{});
    entry->setValueText(1, QStringLiteral("9,9"));
    EXPECT_EQ(entry->firstErrorRow(), -1);
    EXPECT_TRUE(entry->save());
}

TEST_F(MetricViewModelsTest, ClearingAStoredValueDeletesIt) {
    const MetricDefinitionId metric = addSprint();
    addPlayer("Jan", "Kowalski", 7);
    addSession(kToday);
    auto entry = makeEntry();
    entry->setValueText(0, QStringLiteral("4.4"));
    ASSERT_TRUE(entry->save());
    EXPECT_EQ(registry().metrics().listDefinitions()->front().valueCount, 1);

    entry->rememberCell(0);
    entry->setValueText(0, QStringLiteral(""));
    EXPECT_TRUE(entry->dirty());
    entry->revertCell(0);
    EXPECT_FALSE(entry->dirty()) << "Esc brings the stored value back";

    entry->clearValue(0);
    ASSERT_TRUE(entry->save());
    EXPECT_EQ(registry().metrics().listDefinitions()->front().valueCount, 0);
    EXPECT_EQ(entry->metrics()->find(metric.value)->valueCount, 0);
}

TEST_F(MetricViewModelsTest, BooleanAndPlayerScope) {
    MetricDefinitionDraft draft;
    draft.namePl = "Rozgrzewka";
    draft.valueType = MetricValueType::Boolean;
    draft.aggregation = MetricAggregation::Sum;
    draft.scope = MetricScope::Player;
    const auto id = registry().metrics().addDefinition(draft);
    ASSERT_TRUE(id.has_value());
    addPlayer("Jan", "Kowalski", 7);

    auto entry = makeEntry();
    EXPECT_EQ(entry->scope(), QStringLiteral("player"));
    EXPECT_EQ(entry->valueType(), QStringLiteral("boolean"));
    EXPECT_EQ(entry->date(), QStringLiteral("2026-11-01")) << "today";
    EXPECT_FALSE(value(*entry, 0, MetricEntryViewModel::HasValueRole).toBool());
    entry->setChecked(0, true);
    EXPECT_TRUE(value(*entry, 0, MetricEntryViewModel::CheckedRole).toBool());
    ASSERT_TRUE(entry->save());

    entry->openDate(QStringLiteral("2026-10-30"));
    EXPECT_EQ(entry->date(), QStringLiteral("2026-10-30"));
    EXPECT_FALSE(value(*entry, 0, MetricEntryViewModel::HasValueRole).toBool())
        << "another day is another entry";
}

TEST_F(MetricViewModelsTest, MatchScopeOpensTheLatestMatch) {
    MetricDefinitionDraft draft;
    draft.nameEn = "Duels won";
    draft.valueType = MetricValueType::Integer;
    draft.aggregation = MetricAggregation::Sum;
    draft.scope = MetricScope::Match;
    ASSERT_TRUE(registry().metrics().addDefinition(draft).has_value());
    addPlayer("Jan", "Kowalski", 7);
    const MatchId past = addMatch(kLastWeek);
    addMatch(2026y / November / 8d);

    auto entry = makeEntry();
    EXPECT_EQ(entry->scope(), QStringLiteral("match"));
    EXPECT_EQ(entry->targetId(), past.value);
    EXPECT_EQ(entry->matches()->rowCount(), 2);
    entry->setValueText(0, QStringLiteral("2,5"));
    EXPECT_TRUE(value(*entry, 0, MetricEntryViewModel::HasErrorRole).toBool()) << "whole number";
}

TEST_F(MetricViewModelsTest, OpeningAnotherMetricDropsTheChanges) {
    addSprint();
    MetricDefinitionDraft other;
    other.nameEn = "Jump";
    other.scope = MetricScope::Player;
    const auto jump = registry().metrics().addDefinition(other);
    ASSERT_TRUE(jump.has_value());
    addPlayer("Jan", "Kowalski", 7);
    addSession(kToday);
    auto entry = makeEntry();
    entry->setValueText(0, QStringLiteral("4"));
    ASSERT_TRUE(entry->dirty());
    entry->openMetric(jump->value);
    EXPECT_FALSE(entry->dirty());
    EXPECT_EQ(entry->scope(), QStringLiteral("player"));
}

TEST_F(MetricViewModelsTest, PlayerCardShowsLatestWindowAndTrend) {
    const MetricDefinitionId metric = addSprint();
    const PlayerId player = addPlayer("Jan", "Kowalski", 7);
    const TrainingSessionId older = addSession(kLastWeek);
    const TrainingSessionId latest = addSession(kToday);
    const std::vector<MetricValueDraft> first{{player, 4.5}};
    const std::vector<MetricValueDraft> second{{player, 4.3}};
    ASSERT_TRUE(registry().metrics().saveValues(metric, m_team, older, first).has_value());
    ASSERT_TRUE(registry().metrics().saveValues(metric, m_team, latest, second).has_value());

    PlayerMetricsViewModel card;
    card.setPlayerId(player.value);
    ASSERT_EQ(card.count(), 1);
    const QModelIndex row = card.index(0);
    EXPECT_EQ(row.data(PlayerMetricsViewModel::TrendRole).toString(), QStringLiteral("down"));
    EXPECT_TRUE(row.data(PlayerMetricsViewModel::ImprovementRole).toBool())
        << "a faster sprint is better";
    EXPECT_FALSE(row.data(PlayerMetricsViewModel::TrendTextRole).toString().isEmpty());
    EXPECT_EQ(row.data(PlayerMetricsViewModel::LatestDateRole).toString(),
              QStringLiteral("2026-11-01"));
    EXPECT_EQ(row.data(PlayerMetricsViewModel::LatestTextRole).toString(),
              MetricTexts::withUnit(MetricTexts::number(4.3), std::string("s")));
    MetricDefinitionDraft definition;
    definition.unit = "s";
    definition.aggregation = MetricAggregation::Avg;
    EXPECT_EQ(row.data(PlayerMetricsViewModel::WindowTextRole).toString(),
              MetricTexts::window(definition, 4.4, 2));

    PlayerMetricsViewModel other;
    other.setPlayerId(addPlayer("Adam", "Nowak", 9).value);
    EXPECT_EQ(other.count(), 0);
}

TEST_F(MetricViewModelsTest, NumbersHaveNoTrailingZeros) {
    EXPECT_EQ(MetricTexts::number(12.0), QStringLiteral("12"));
    EXPECT_EQ(MetricTexts::number(-0.0), QStringLiteral("0"));
    EXPECT_EQ(MetricTexts::number(4.40, 2), MetricTexts::number(4.4));
    EXPECT_EQ(MetricTexts::number(4.4444, 2), MetricTexts::number(4.44));
}

} // namespace
