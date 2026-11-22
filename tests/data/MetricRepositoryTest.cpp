#include "data/repositories/MetricRepository.hpp"

#include "RepositoryFixture.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <optional>
#include <vector>

using namespace fdss::core;
using namespace fdss::data;
using namespace fdss::tests;
using namespace std::chrono;

namespace {

// kMatch of the fixture is on 2026-11-01, kSession on 2026-11-02.
constexpr Date kMatchDate = 2026y / November / 1d;
constexpr Date kSessionDate = 2026y / November / 2d;
constexpr Date kDay = 2026y / November / 5d;

class MetricRepositoryTest : public RepositoryTest {
protected:
    void SetUp() override {
        RepositoryTest::SetUp();
        seedCalendar();
    }

    [[nodiscard]] MetricRepository repository() { return MetricRepository(*m_db); }

    static MetricDefinitionDraft sprint() {
        MetricDefinitionDraft draft;
        draft.nameEn = "Sprint 30 m";
        draft.namePl = "Sprint 30 m";
        draft.unit = "s";
        draft.min = 3.0;
        draft.max = 10.0;
        draft.higherIsBetter = false;
        return draft;
    }

    MetricDefinitionId create(const MetricDefinitionDraft& draft, std::string_view key) {
        const auto id = repository().createDefinition(draft, key);
        EXPECT_TRUE(id.has_value()) << id.error().detail;
        return id.value_or(MetricDefinitionId{});
    }

    void save(MetricDefinitionId id, const MetricTarget& target, Date date,
              std::vector<MetricValueDraft> values) {
        const auto saved = repository().saveValues(id, target, date, values);
        ASSERT_TRUE(saved.has_value()) << saved.error().detail;
    }
};

TEST_F(MetricRepositoryTest, DefinitionRoundTrip) {
    MetricDefinitionDraft draft = sprint();
    draft.nameEn = "  Sprint 30 m ";
    const MetricDefinitionId id = create(draft, "sprint_30_m");

    const auto stored = repository().getDefinition(id);
    ASSERT_TRUE(stored.has_value()) << stored.error().detail;
    EXPECT_EQ(stored->key, "sprint_30_m");
    EXPECT_EQ(stored->nameEn, "Sprint 30 m") << "names are stored trimmed";
    EXPECT_EQ(stored->unit, "s");
    EXPECT_EQ(stored->min, 3.0);
    EXPECT_EQ(stored->max, 10.0);
    EXPECT_EQ(stored->valueType, MetricValueType::Decimal);
    EXPECT_EQ(stored->scope, MetricScope::Training);
    EXPECT_FALSE(stored->higherIsBetter);

    MetricDefinitionDraft changed = sprint();
    changed.namePl = "Bieg 30 m";
    changed.nameEn = "";
    changed.unit.reset();
    changed.min.reset();
    changed.aggregation = MetricAggregation::Min;
    ASSERT_TRUE(repository().updateDefinition(id, changed).has_value());
    const auto updated = repository().getDefinition(id);
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(updated->key, "sprint_30_m") << "the key never changes";
    EXPECT_EQ(static_cast<const MetricDefinitionDraft&>(*updated), changed);

    const auto all = repository().listDefinitions();
    ASSERT_TRUE(all.has_value());
    EXPECT_EQ(all->size(), 1U);
}

TEST_F(MetricRepositoryTest, KeyIsUnique) {
    create(sprint(), "sprint_30_m");
    EXPECT_EQ(repository().keyExists("sprint_30_m"), true);
    EXPECT_EQ(repository().keyExists("other"), false);

    const auto again = repository().createDefinition(sprint(), "sprint_30_m");
    ASSERT_FALSE(again.has_value());
    EXPECT_EQ(again.error().code, ErrorCode::Conflict);
    EXPECT_EQ(again.error().field, "key");
}

TEST_F(MetricRepositoryTest, DefinitionWithValuesCannotBeDeleted) {
    const MetricDefinitionId id = create(sprint(), "sprint_30_m");
    save(id, kSession, kSessionDate, {{kPlayer, 4.3}});
    EXPECT_EQ(repository().valueCount(id), 1);

    const auto removed = repository().removeDefinition(id);
    ASSERT_FALSE(removed.has_value());
    EXPECT_EQ(removed.error().code, ErrorCode::Conflict);

    save(id, kSession, kSessionDate, {{kPlayer, std::nullopt}});
    EXPECT_EQ(repository().valueCount(id), 0);
    EXPECT_TRUE(repository().removeDefinition(id).has_value());
    EXPECT_EQ(repository().getDefinition(id).error().code, ErrorCode::NotFound);
}

TEST_F(MetricRepositoryTest, ValuesBelongToTheirTarget) {
    const MetricDefinitionId id = create(sprint(), "sprint_30_m");
    save(id, kSession, kSessionDate, {{kPlayer, 4.3}, {kOtherPlayer, 4.5}});
    save(id, kMatch, kMatchDate, {{kPlayer, 4.4}});
    save(id, kDay, kDay, {{kPlayer, 4.2}});

    const auto ofSession = repository().listValues(id, kSession);
    ASSERT_TRUE(ofSession.has_value());
    ASSERT_EQ(ofSession->size(), 2U);
    EXPECT_EQ(ofSession->at(0).playerId, kPlayer);
    EXPECT_EQ(ofSession->at(0).value, 4.3);
    EXPECT_EQ(ofSession->at(0).sessionId, kSession);
    EXPECT_EQ(ofSession->at(0).matchId, std::nullopt);
    EXPECT_EQ(ofSession->at(0).date, kSessionDate);

    const auto ofMatch = repository().listValues(id, kMatch);
    ASSERT_TRUE(ofMatch.has_value());
    ASSERT_EQ(ofMatch->size(), 1U);
    EXPECT_EQ(ofMatch->at(0).value, 4.4);

    const auto ofDay = repository().listValues(id, kDay);
    ASSERT_TRUE(ofDay.has_value());
    ASSERT_EQ(ofDay->size(), 1U) << "the session and the match are other entries";
    EXPECT_EQ(ofDay->at(0).value, 4.2);
    EXPECT_EQ(repository().listValues(id, MetricTarget{kSessionDate})->size(), 0U)
        << "a day entry never shows the values of a session on that day";
}

TEST_F(MetricRepositoryTest, SaveReplacesChangesAndKeepsOthers) {
    const MetricDefinitionId id = create(sprint(), "sprint_30_m");
    ASSERT_TRUE(sql("INSERT INTO import_log (id, timestamp, file, hash, type) "
                    "VALUES (1, '2026-11-01T10:00:00Z', 'a.csv', 'h', 'metrics')"));
    ASSERT_TRUE(sql("INSERT INTO metric_value (metric_id, player_id, session_id, date, value, "
                    "import_id) VALUES (?, 1, 1, '2026-11-02', 4.3, 1)",
                    {QVariant::fromValue(id.value)}));

    save(id, kSession, kSessionDate, {{kPlayer, 4.3}});
    EXPECT_EQ(count("SELECT count(*) FROM metric_value WHERE import_id = 1"), 1)
        << "an unchanged value keeps its row and its import";

    save(id, kSession, kSessionDate, {{kOtherPlayer, 4.6}});
    EXPECT_EQ(count("SELECT count(*) FROM metric_value WHERE player_id = 1"), 1)
        << "a player who is not in the list keeps the value";

    save(id, kSession, kSessionDate, {{kPlayer, 4.1}});
    const auto values = repository().listValues(id, kSession);
    ASSERT_TRUE(values.has_value());
    ASSERT_EQ(values->size(), 2U);
    EXPECT_EQ(values->at(0).value, 4.1);
    // P17 decision 14 (review of P18): a corrected imported value keeps its import, so the undo
    // of that import still removes it.
    EXPECT_EQ(count("SELECT count(*) FROM metric_value WHERE import_id = 1"), 1);
}

TEST_F(MetricRepositoryTest, FailedSaveChangesNothing) {
    const MetricDefinitionId id = create(sprint(), "sprint_30_m");
    save(id, kSession, kSessionDate, {{kPlayer, 4.3}});
    const std::array values{MetricValueDraft{kPlayer, 4.0}, MetricValueDraft{kNoPlayer, 4.0}};
    const auto saved = repository().saveValues(id, kSession, kSessionDate, values);
    ASSERT_FALSE(saved.has_value());
    EXPECT_EQ(saved.error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(repository().listValues(id, kSession)->at(0).value, 4.3);

    const auto missing =
        repository().saveValues(MetricDefinitionId{999}, kSession, kSessionDate, values);
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, ErrorCode::NotFound);
}

TEST_F(MetricRepositoryTest, PlayerValuesAreOldestFirst) {
    const MetricDefinitionId id = create(sprint(), "sprint_30_m");
    save(id, kDay, kDay, {{kPlayer, 4.2}});
    save(id, kSession, kSessionDate, {{kPlayer, 4.3}});
    save(id, kMatch, kMatchDate, {{kPlayer, 4.4}, {kOtherPlayer, 5.0}});

    const auto values = repository().listPlayerValues(kPlayer);
    ASSERT_TRUE(values.has_value());
    ASSERT_EQ(values->size(), 3U);
    EXPECT_EQ(values->at(0).value, 4.4);
    EXPECT_EQ(values->at(1).value, 4.3);
    EXPECT_EQ(values->at(2).value, 4.2);
}

// P17 decision 8: a value of a session or a match is read with the current date of that session or
// match, whatever date was stored with it; only a value of scope player keeps its own date.
TEST_F(MetricRepositoryTest, ValueIsDatedByItsSessionOrMatchWhenRead) {
    const MetricDefinitionId id = create(sprint(), "sprint_30_m");
    save(id, kSession, kSessionDate, {{kPlayer, 4.3}});
    save(id, kMatch, kMatchDate, {{kPlayer, 4.4}});
    save(id, kDay, kDay, {{kPlayer, 4.2}});

    ASSERT_TRUE(sql("UPDATE training_session SET date = '2026-11-20' WHERE id = 1"));
    ASSERT_TRUE(sql("UPDATE match SET date = '2026-11-21' WHERE id = 1"));
    EXPECT_EQ(count("SELECT count(*) FROM metric_value WHERE date = '2026-11-02'"), 1)
        << "the stored date stays as it was entered";

    const auto values = repository().listPlayerValues(kPlayer);
    ASSERT_TRUE(values.has_value());
    ASSERT_EQ(values->size(), 3U);
    EXPECT_EQ(values->at(0).value, 4.2);
    EXPECT_EQ(values->at(0).date, kDay) << "scope player keeps its own date";
    EXPECT_EQ(values->at(1).value, 4.3);
    EXPECT_EQ(values->at(1).date, 2026y / November / 20d) << "the date of the moved session";
    EXPECT_EQ(values->at(2).value, 4.4);
    EXPECT_EQ(values->at(2).date, 2026y / November / 21d) << "the date of the moved match";
    EXPECT_EQ(repository().listValues(id, kSession)->front().date, 2026y / November / 20d);
}

} // namespace
