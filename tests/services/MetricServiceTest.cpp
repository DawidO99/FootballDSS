#include "services/MetricService.hpp"

#include "ServiceFixture.hpp"
#include "core/domain/Permissions.hpp"
#include "data/repositories/MatchRepository.hpp"
#include "data/repositories/MetricRepository.hpp"
#include "data/repositories/PlayerRepository.hpp"
#include "data/repositories/TrainingRepository.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace fdss::core;
using namespace fdss::data;
using namespace fdss::services;
using namespace fdss::tests;
using namespace std::chrono;

namespace {

// Of the fixture: match 1 on 2026-11-01, session 1 on 2026-11-02; today is 2026-11-01.
constexpr Date kSessionDay = 2026y / November / 2d;

MetricDefinitionDraft sprint() {
    MetricDefinitionDraft draft;
    draft.namePl = "Sprint 30 m";
    draft.nameEn = "Sprint 30 m";
    draft.unit = "s";
    draft.min = 3.0;
    draft.max = 10.0;
    draft.aggregation = MetricAggregation::Avg;
    draft.scope = MetricScope::Training;
    draft.higherIsBetter = false;
    return draft;
}

MetricDefinitionDraft jump() {
    MetricDefinitionDraft draft;
    draft.namePl = "Skok w dal";
    draft.unit = "cm";
    draft.valueType = MetricValueType::Integer;
    draft.aggregation = MetricAggregation::Max;
    draft.scope = MetricScope::Player;
    return draft;
}

class MetricServiceTest : public ServiceTest {
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
        m_metrics = std::make_unique<MetricRepository>(*m_db);
        m_players = std::make_unique<PlayerRepository>(*m_db);
        m_training = std::make_unique<TrainingRepository>(*m_db);
        m_matches = std::make_unique<MatchRepository>(*m_db);
        m_service = std::make_unique<MetricService>(*m_db, *m_metrics, *m_players, *m_training,
                                                    *m_matches, m_session, m_clock);
    }

    void logInAs(Role role) {
        m_session.clear();
        const StaffUserId id = addUser(std::string(toDbText(role)).c_str(), role);
        logIn(id);
    }

    MetricDefinitionId add(const MetricDefinitionDraft& draft) {
        const auto id = m_service->addDefinition(draft);
        EXPECT_TRUE(id.has_value()) << id.error().detail;
        return id.value_or(MetricDefinitionId{});
    }

    void save(MetricDefinitionId id, const MetricTarget& target,
              std::vector<MetricValueDraft> values) {
        const auto saved = m_service->saveValues(id, kTeam, target, values);
        ASSERT_TRUE(saved.has_value()) << saved.error().detail;
        ASSERT_TRUE(saved->empty());
    }

    void moveTodayTo(Date day) { m_clock.advance(sys_days{day} - sys_days{m_clock.today()}); }

    std::unique_ptr<MetricRepository> m_metrics;
    std::unique_ptr<PlayerRepository> m_players;
    std::unique_ptr<TrainingRepository> m_training;
    std::unique_ptr<MatchRepository> m_matches;
    std::unique_ptr<MetricService> m_service;
};

TEST_F(MetricServiceTest, PermissionPerRole) {
    for (const Role role : allValues<Role>()) {
        SCOPED_TRACE(toDbText(role));
        logInAs(role);
        EXPECT_TRUE(m_service->listDefinitions().has_value()) << "everyone reads";
        EXPECT_TRUE(m_service->playerSummary(kPlayer).has_value());

        MetricDefinitionDraft draft = sprint();
        draft.nameEn = "Sprint " + std::string(toDbText(role));
        const auto added = m_service->addDefinition(draft);
        const bool allowed = isAllowed(role, Permission::ManageMetrics);
        EXPECT_EQ(added.has_value(), allowed);
        if (!allowed) {
            expectError(added, ErrorCode::PermissionDenied);
            continue;
        }
        EXPECT_TRUE(m_service->sheet(*added, kTeam, kSession).has_value());
        save(*added, kSession, {{kPlayer, 4.3}});
    }
    // The table of SPEC section 5: Coach, Assistant and Analyst manage metrics.
    EXPECT_TRUE(isAllowed(Role::Coach, Permission::ManageMetrics));
    EXPECT_TRUE(isAllowed(Role::Assistant, Permission::ManageMetrics));
    EXPECT_FALSE(isAllowed(Role::Physio, Permission::ManageMetrics));
    EXPECT_TRUE(isAllowed(Role::Analyst, Permission::ManageMetrics));
}

TEST_F(MetricServiceTest, PhysioCannotChangeAnything) {
    logInAs(Role::Coach);
    const MetricDefinitionId id = add(sprint());
    logInAs(Role::Physio);
    expectError(m_service->updateDefinition(id, sprint()), ErrorCode::PermissionDenied);
    expectError(m_service->deleteDefinition(id), ErrorCode::PermissionDenied);
    const std::vector<MetricValueDraft> values{{kPlayer, 4.3}};
    expectError(m_service->saveValues(id, kTeam, kSession, values), ErrorCode::PermissionDenied);
    EXPECT_EQ(m_metrics->valueCount(id), 0);
}

TEST_F(MetricServiceTest, NobodyLoggedInReadsNothing) {
    expectError(m_service->listDefinitions(), ErrorCode::PermissionDenied);
    expectError(m_service->playerSummary(kPlayer), ErrorCode::PermissionDenied);
}

TEST_F(MetricServiceTest, KeyIsGeneratedAndUnique) {
    logInAs(Role::Analyst);
    const MetricDefinitionId id = add(sprint());
    EXPECT_EQ(m_service->definition(id)->key, "sprint_30_m");

    MetricDefinitionDraft similar = sprint();
    similar.nameEn = "SPRINT-30-M";
    expectError(m_service->addDefinition(similar), ErrorCode::Conflict, "key");

    MetricDefinitionDraft renamed = sprint();
    renamed.nameEn = "Sprint thirty";
    ASSERT_TRUE(m_service->updateDefinition(id, renamed).has_value());
    EXPECT_EQ(m_service->definition(id)->key, "sprint_30_m") << "the key stays";

    const MetricDefinitionId polishOnly = add(jump());
    EXPECT_EQ(m_service->definition(polishOnly)->key, "skok_w_dal");
}

TEST_F(MetricServiceTest, InvalidDefinitionIsRefused) {
    logInAs(Role::Coach);
    MetricDefinitionDraft draft = sprint();
    draft.namePl.clear();
    draft.nameEn.clear();
    expectError(m_service->addDefinition(draft), ErrorCode::InvalidInput, "name");
    draft = sprint();
    draft.max = 1.0;
    expectError(m_service->addDefinition(draft), ErrorCode::InvalidInput, "max");
    EXPECT_TRUE(m_service->listDefinitions()->empty());
}

TEST_F(MetricServiceTest, DeleteWithValuesIsRefused) {
    logInAs(Role::Coach);
    const MetricDefinitionId id = add(sprint());
    save(id, kSession, {{kPlayer, 4.3}});
    expectError(m_service->deleteDefinition(id), ErrorCode::Conflict, "metric_in_use");
    EXPECT_EQ(m_service->listDefinitions()->front().valueCount, 1);

    save(id, kSession, {{kPlayer, std::nullopt}});
    const auto deleted = m_service->deleteDefinition(id);
    ASSERT_TRUE(deleted.has_value()) << deleted.error().detail;
    EXPECT_TRUE(m_service->listDefinitions()->empty());

    const auto restored = m_service->restoreDefinition(*deleted);
    ASSERT_TRUE(restored.has_value()) << restored.error().detail;
    const auto again = m_service->definition(*restored);
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(again->key, deleted->key);
    EXPECT_EQ(static_cast<const MetricDefinitionDraft&>(*again),
              static_cast<const MetricDefinitionDraft&>(*deleted));
}

TEST_F(MetricServiceTest, TypeAndScopeAreLockedByValues) {
    logInAs(Role::Coach);
    const MetricDefinitionId id = add(sprint());
    MetricDefinitionDraft changed = sprint();
    changed.valueType = MetricValueType::Integer;
    changed.min = 3.0;
    ASSERT_TRUE(m_service->updateDefinition(id, changed).has_value()) << "no values yet";

    save(id, kSession, {{kPlayer, 4.0}});
    changed.valueType = MetricValueType::Decimal;
    expectError(m_service->updateDefinition(id, changed), ErrorCode::Conflict, "metric_locked");
    changed.valueType = MetricValueType::Integer;
    changed.scope = MetricScope::Match;
    expectError(m_service->updateDefinition(id, changed), ErrorCode::Conflict, "metric_locked");

    changed.scope = MetricScope::Training;
    changed.max = 5.0;
    changed.unit = "sec";
    changed.aggregation = MetricAggregation::Min;
    EXPECT_TRUE(m_service->updateDefinition(id, changed).has_value())
        << "range, unit and aggregation may change; the stored value stays";
    EXPECT_EQ(m_metrics->listValues(id, kSession)->front().value, 4.0);
}

TEST_F(MetricServiceTest, SheetListsThePlayersWithTheirValues) {
    logInAs(Role::Coach);
    const MetricDefinitionId id = add(sprint());
    save(id, kSession, {{kOtherPlayer, 4.5}});
    const auto sheet = m_service->sheet(id, kTeam, kSession);
    ASSERT_TRUE(sheet.has_value()) << sheet.error().detail;
    EXPECT_EQ(sheet->date, kSessionDay);
    ASSERT_EQ(sheet->rows.size(), 2U);
    for (const MetricSheetRow& row : sheet->rows) {
        EXPECT_EQ(row.value, row.player.id == kOtherPlayer ? std::optional(4.5) : std::nullopt);
    }
}

TEST_F(MetricServiceTest, TargetMustFitTheScopeAndTheTeam) {
    logInAs(Role::Coach);
    const MetricDefinitionId training = add(sprint());
    expectError(m_service->sheet(training, kTeam, kMatch), ErrorCode::InvalidInput, "scope");
    expectError(m_service->sheet(training, kTeam, MetricTarget{kSessionDay}),
                ErrorCode::InvalidInput, "scope");
    expectError(m_service->sheet(training, kOtherTeam, kSession), ErrorCode::InvalidInput, "team");
    expectError(m_service->sheet(training, kTeam, TrainingSessionId{999}), ErrorCode::NotFound);
    const std::vector<MetricValueDraft> values{{kPlayer, 4.3}};
    expectError(m_service->saveValues(training, kTeam, kMatch, values), ErrorCode::InvalidInput,
                "scope");
}

TEST_F(MetricServiceTest, WrongRowsAreReportedAndNothingIsSaved) {
    logInAs(Role::Coach);
    const MetricDefinitionId id = add(sprint());
    ASSERT_TRUE(sql("INSERT INTO player (id, team_id, first_name, last_name, primary_position) "
                    "VALUES (3, 2, 'Obcy', 'Gracz', 'gk')"));
    const std::vector<MetricValueDraft> values{
        {kPlayer, 2.0}, {kOtherPlayer, 4.4}, {kOtherPlayer, 4.5}, {PlayerId{3}, 4.0}};
    const auto saved = m_service->saveValues(id, kTeam, kSession, values);
    ASSERT_TRUE(saved.has_value()) << saved.error().detail;
    ASSERT_EQ(saved->size(), 3U);
    EXPECT_EQ(saved->at(0).player, kPlayer);
    EXPECT_EQ(saved->at(0).error.field, "value_range");
    EXPECT_EQ(saved->at(1).error.field, "player_id") << "given twice";
    EXPECT_EQ(saved->at(2).error.field, "player_id") << "another team";
    EXPECT_EQ(m_metrics->valueCount(id), 0);
}

TEST_F(MetricServiceTest, PlayerScopeUsesTheDay) {
    logInAs(Role::Coach);
    const MetricDefinitionId id = add(jump());
    const Date day = 2026y / October / 20d;
    save(id, MetricTarget{day}, {{kPlayer, 512.0}});
    const auto stored = m_metrics->listValues(id, MetricTarget{day});
    ASSERT_EQ(stored->size(), 1U);
    EXPECT_EQ(stored->front().date, day);
    const std::vector<MetricValueDraft> half{{kPlayer, 512.5}};
    const auto saved = m_service->saveValues(id, kTeam, MetricTarget{day}, half);
    ASSERT_TRUE(saved.has_value());
    ASSERT_EQ(saved->size(), 1U);
    EXPECT_EQ(saved->front().error.field, "value") << "an integer metric";
}

TEST_F(MetricServiceTest, SummaryHasLatestPreviousAndTheWindow) {
    logInAs(Role::Coach);
    const MetricDefinitionId id = add(sprint());
    MetricDefinitionDraft perDay = sprint();
    perDay.nameEn = "Sprint test day";
    perDay.scope = MetricScope::Player;
    const MetricDefinitionId daily = add(perDay);
    // Today 2026-11-30: the window is 2026-11-03 ... 2026-11-30.
    save(daily, MetricTarget{2026y / October / 20d}, {{kPlayer, 4.9}});
    save(daily, MetricTarget{2026y / November / 2d}, {{kPlayer, 4.6}});
    save(daily, MetricTarget{2026y / November / 3d}, {{kPlayer, 4.4}});
    save(daily, MetricTarget{2026y / November / 20d}, {{kPlayer, 4.2}});
    save(daily, MetricTarget{2026y / November / 30d}, {{kPlayer, 4.3}});
    save(daily, MetricTarget{2026y / December / 5d}, {{kPlayer, 3.5}});
    moveTodayTo(2026y / November / 30d);

    const auto summary = m_service->playerSummary(kPlayer);
    ASSERT_TRUE(summary.has_value()) << summary.error().detail;
    ASSERT_EQ(summary->size(), 1U) << "a metric without values is not listed";
    const PlayerMetricSummary& row = summary->front();
    EXPECT_EQ(row.metric.id, daily);
    EXPECT_EQ(row.latest, 4.3) << "a value after today waits";
    EXPECT_EQ(row.latestDate, 2026y / November / 30d);
    EXPECT_EQ(row.previous, 4.2);
    EXPECT_EQ(row.trend, MetricTrend::Up);
    EXPECT_FALSE(isImprovement(row.trend, row.metric.higherIsBetter)) << "slower";
    EXPECT_EQ(row.windowCount, 3);
    ASSERT_TRUE(row.window.has_value());
    EXPECT_DOUBLE_EQ(*row.window, (4.4 + 4.2 + 4.3) / 3.0);

    save(id, kSession, {{kOtherPlayer, 4.1}});
    EXPECT_EQ(m_service->playerSummary(kPlayer)->size(), 1U) << "another player's value";
}

TEST_F(MetricServiceTest, SummaryWithoutValuesInTheWindow) {
    logInAs(Role::Coach);
    MetricDefinitionDraft perDay = sprint();
    perDay.scope = MetricScope::Player;
    const MetricDefinitionId id = add(perDay);
    save(id, MetricTarget{2026y / September / 1d}, {{kPlayer, 4.4}});
    const auto summary = m_service->playerSummary(kPlayer);
    ASSERT_EQ(summary->size(), 1U);
    EXPECT_EQ(summary->front().latest, 4.4);
    EXPECT_EQ(summary->front().previous, std::nullopt);
    EXPECT_EQ(summary->front().trend, MetricTrend::None);
    EXPECT_EQ(summary->front().window, std::nullopt);
    EXPECT_EQ(summary->front().windowCount, 0);
}

} // namespace
