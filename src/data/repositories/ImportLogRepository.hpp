#pragma once

#include "core/domain/DateTypes.hpp"
#include "core/domain/Ids.hpp"
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

struct ImportReport {
    std::optional<ImportMappingRecord> mapping;
    int created = 0;
    int updated = 0;
    int skipped = 0;
    std::optional<core::Timestamp> undoneAt;

    friend bool operator==(const ImportReport&, const ImportReport&) = default;
};

struct ImportLogDraft {
    core::Timestamp timestamp{};
    std::string file; // the file name the user chose, without the folder
    std::string hash; // SHA-256, lower-case hex
    std::string type; // "squad"; P18 adds more
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
