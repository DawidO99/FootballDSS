#include "data/repositories/ImportLogRepository.hpp"

#include "RepositoryFixture.hpp"

#include <gtest/gtest.h>

#include <chrono>

using namespace fdss::core;
using namespace fdss::data;
using namespace fdss::tests;
using namespace std::chrono;

namespace {

constexpr Timestamp kTime{sys_days{2026y / November / 20d} + hours{18}};

class ImportLogRepositoryTest : public RepositoryTest {
protected:
    [[nodiscard]] ImportLogRepository repository() { return ImportLogRepository(*m_db); }

    static ImportLogDraft draft(std::string hash = "h1", std::string headerKey = "imie|nazwisko") {
        ImportLogDraft result;
        result.timestamp = kTime;
        result.file = "Kadra żółta.csv";
        result.hash = std::move(hash);
        result.type = "squad";
        result.rowsOk = 20;
        result.rowsFailed = 2;
        result.report.created = 18;
        result.report.updated = 2;
        result.report.skipped = 1;
        result.report.mapping =
            ImportMappingRecord{std::move(headerKey),
                                {"first_name", "last_name", ""},
                                {{"name_order", "first_last"}, {"default_position", "cm"}}};
        return result;
    }

    ImportId create(const ImportLogDraft& value) {
        const auto id = repository().create(value);
        EXPECT_TRUE(id.has_value()) << id.error().detail;
        return id.value_or(ImportId{});
    }
};

TEST_F(ImportLogRepositoryTest, CreateAndGetRoundTrip) {
    const ImportLogDraft value = draft();
    const ImportId id = create(value);
    const auto stored = repository().get(id);
    ASSERT_TRUE(stored.has_value()) << stored.error().detail;
    EXPECT_EQ(static_cast<const ImportLogDraft&>(*stored), value);
    EXPECT_EQ(stored->id, id);
    EXPECT_EQ(count("SELECT count(*) FROM import_log WHERE timestamp = '2026-11-20T18:00:00Z'"), 1);
}

TEST_F(ImportLogRepositoryTest, ListByHashIsNewestFirstAndPerType) {
    const ImportId first = create(draft("same"));
    const ImportId second = create(draft("same"));
    create(draft("other"));
    ImportLogDraft training = draft("same");
    training.type = "training";
    create(training);
    const auto found = repository().listByHash("same", "squad");
    ASSERT_TRUE(found.has_value()) << found.error().detail;
    ASSERT_EQ(found->size(), 2U);
    EXPECT_EQ((*found)[0].id, second);
    EXPECT_EQ((*found)[1].id, first);
    EXPECT_TRUE(repository().listByHash("none", "squad")->empty());
}

TEST_F(ImportLogRepositoryTest, LatestMappingByHeaderKey) {
    create(draft("a", "imie|nazwisko"));
    ImportLogDraft newer = draft("b", "imie|nazwisko");
    newer.report.mapping->columns = {"last_name", "first_name", ""};
    create(newer);
    create(draft("c", "zawodnik"));
    const auto mapping = repository().latestMapping("squad", "imie|nazwisko");
    ASSERT_TRUE(mapping.has_value()) << mapping.error().detail;
    ASSERT_TRUE(mapping->has_value());
    EXPECT_EQ((*mapping)->columns, (std::vector<std::string>{"last_name", "first_name", ""}));
    EXPECT_FALSE(repository().latestMapping("squad", "nr")->has_value());
    EXPECT_FALSE(repository().latestMapping("training", "imie|nazwisko")->has_value());
}

TEST_F(ImportLogRepositoryTest, SetReportMarksTheUndo) {
    const ImportId id = create(draft());
    ImportReport report = draft().report;
    report.undoneAt = kTime + hours{1};
    ASSERT_TRUE(repository().setReport(id, report).has_value());
    EXPECT_EQ(repository().get(id)->report.undoneAt, kTime + hours{1});
    const auto missing = repository().setReport(ImportId{99}, report);
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, ErrorCode::NotFound);
}

TEST_F(ImportLogRepositoryTest, DamagedReportFailsTheRead) {
    const ImportId id = create(draft());
    ASSERT_TRUE(
        sql("UPDATE import_log SET report_json = 'not json' WHERE id = ?", {qlonglong(id.value)}));
    const auto stored = repository().get(id);
    ASSERT_FALSE(stored.has_value());
    EXPECT_EQ(stored.error().code, ErrorCode::Database);
}

TEST_F(ImportLogRepositoryTest, MissingReportIsEmpty) {
    ASSERT_TRUE(sql("INSERT INTO import_log (id, timestamp, file, hash, type) "
                    "VALUES (5, '2026-11-01T10:00:00Z', 'a.csv', 'x', 'squad')"));
    const auto stored = repository().get(ImportId{5});
    ASSERT_TRUE(stored.has_value()) << stored.error().detail;
    EXPECT_EQ(stored->report, ImportReport{});
}

} // namespace

// Packet P18: the rows of a training or match import survive the JSON.
TEST_F(ImportLogRepositoryTest, ImportedRowsRoundTrip) {
    ImportLogDraft value = draft();
    value.type = "matches";
    AttendanceDraft before;
    before.playerId = PlayerId{3};
    before.status = AttendanceStatus::Excused;
    AttendanceDraft after = before;
    after.status = AttendanceStatus::Present;
    after.rpe = 7;
    after.minutesOverride = 60;
    value.report.attendance = {ImportedAttendance{TrainingSessionId{4}, after, before},
                               ImportedAttendance{TrainingSessionId{5}, after, std::nullopt}};
    AppearanceDraft appearance;
    appearance.playerId = PlayerId{3};
    appearance.started = true;
    appearance.minuteOn = 0;
    appearance.minuteOff = 75;
    appearance.position = Position::Cb;
    appearance.rating = 7;
    value.report.appearances = {ImportedAppearance{MatchId{2}, appearance, std::nullopt}};
    value.report.metricValues = {ImportedMetricValue{MetricDefinitionId{1}, std::nullopt,
                                                     MatchId{2}, PlayerId{3}, 4.35, 4.5}};
    value.report.createdOpponents = {OpponentId{8}};
    value.report.counts = {{"goals_not_imported", 2}};
    const ImportId id = create(value);
    const auto stored = repository().get(id);
    ASSERT_TRUE(stored.has_value()) << stored.error().detail;
    EXPECT_EQ(stored->report, value.report);
}
