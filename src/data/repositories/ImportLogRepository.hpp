#pragma once

#include "core/domain/Appearance.hpp"
#include "core/domain/Attendance.hpp"
#include "core/domain/DateTypes.hpp"
#include "core/domain/Ids.hpp"
#include "core/domain/Match.hpp"
#include "core/domain/TrainingSession.hpp"
#include "core/util/Error.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// `import_log` (SPEC section 6): one row per committed import, the file hash to warn about the same
// file, and `report_json` with what the import did, the confirmed column mapping (offered again
// for the same header set, packet P16 decision 4) and the time of its undo.

namespace fdss::data {

class DatabaseManager;

struct ImportMappingRecord {
    std::string headerKey;                      // core::headerKey() of the file
    std::vector<std::string> columns;           // the field key of every column, empty = ignored
    std::map<std::string, std::string> options; // per import type, e.g. "name_order"

    friend bool operator==(const ImportMappingRecord&, const ImportMappingRecord&) = default;
};

// The rows a training or match import wrote (packet P18 decisions 3 and 10): the values written
// and, for a row it overwrote, the values before. The undo restores an overwritten row while it
// still holds the written values, and counts a created row that was changed after the import.
struct ImportedAttendance {
    core::TrainingSessionId session;
    core::AttendanceDraft written;
    std::optional<core::AttendanceDraft> previous;

    friend bool operator==(const ImportedAttendance&, const ImportedAttendance&) = default;
};

struct ImportedAppearance {
    core::MatchId match;
    core::AppearanceDraft written;
    std::optional<core::AppearanceDraft> previous;

    friend bool operator==(const ImportedAppearance&, const ImportedAppearance&) = default;
};

struct ImportedMetricValue {
    core::MetricDefinitionId metric;
    std::optional<core::TrainingSessionId> session;
    std::optional<core::MatchId> match;
    core::PlayerId player;
    double written = 0;
    std::optional<double> previous;

    friend bool operator==(const ImportedMetricValue&, const ImportedMetricValue&) = default;
};

// A session or match the import created, as written: the undo counts one changed after the import
// (a note, another date) before it deletes it (review of P18).
struct ImportedSession {
    core::TrainingSessionId id;
    core::TrainingSessionDraft written;

    friend bool operator==(const ImportedSession&, const ImportedSession&) = default;
};

struct ImportedMatch {
    core::MatchId id;
    core::MatchDraft written;
    std::optional<int> goalsFor;
    std::optional<int> goalsAgainst;

    friend bool operator==(const ImportedMatch&, const ImportedMatch&) = default;
};

struct ImportReport {
    std::optional<ImportMappingRecord> mapping;
    int created = 0;
    int updated = 0;
    int skipped = 0;
    std::optional<core::Timestamp> undoneAt;
    // Training and match imports (packet P18).
    std::vector<ImportedSession> sessions;
    std::vector<ImportedMatch> matches;
    std::vector<ImportedAttendance> attendance;
    std::vector<ImportedAppearance> appearances;
    std::vector<ImportedMetricValue> metricValues;
    std::vector<core::OpponentId> createdOpponents; // opponent has no import_id
    std::map<std::string, int> counts;              // for the summary, e.g. "goals_not_imported"

    friend bool operator==(const ImportReport&, const ImportReport&) = default;
};

struct ImportLogDraft {
    core::Timestamp timestamp{};
    std::string file; // the file name the user chose, without the folder
    std::string hash; // SHA-256, lower-case hex
    std::string type; // "squad", "training", "matches"
    int rowsOk = 0;
    int rowsFailed = 0;
    ImportReport report;

    friend bool operator==(const ImportLogDraft&, const ImportLogDraft&) = default;
};

struct ImportLogEntry : ImportLogDraft {
    core::ImportId id;

    friend bool operator==(const ImportLogEntry&, const ImportLogEntry&) = default;
};

// Stored report_json that is not a report: Database (a damaged database), like other unreadable
// values.
class ImportLogRepository {
public:
    explicit ImportLogRepository(DatabaseManager& db) : m_db(db) {}

    [[nodiscard]] core::Result<core::ImportId> create(const ImportLogDraft& draft);
    [[nodiscard]] core::Result<ImportLogEntry> get(core::ImportId id) const;
    // Newest first.
    [[nodiscard]] core::Result<std::vector<ImportLogEntry>> listByHash(std::string_view hash,
                                                                       std::string_view type) const;
    // The mapping of the newest import of the type with this header key.
    [[nodiscard]] core::Result<std::optional<ImportMappingRecord>>
    latestMapping(std::string_view type, std::string_view headerKey) const;
    [[nodiscard]] core::Status setReport(core::ImportId id, const ImportReport& report);

private:
    DatabaseManager& m_db;
};

} // namespace fdss::data
