#pragma once

#include "core/domain/DateTypes.hpp"
#include "core/domain/Ids.hpp"
#include "core/parsing/SquadImport.hpp"
#include "core/util/Error.hpp"
#include "services/import/ImportFile.hpp"

#include <QFuture>
#include <QString>

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace fdss::data {
class DatabaseManager;
class ImportLogRepository;
class PlayerRepository;
} // namespace fdss::data

namespace fdss::services {

class Clock;
class Session;

// import_log.type of a squad import.
inline constexpr std::string_view kSquadImportType = "squad";

enum class InvalidRows { Skip, Abort };

struct SquadImportOptions {
    core::ExistingPolicy existing = core::ExistingPolicy::Skip;
    InvalidRows invalid = InvalidRows::Skip;
};

struct SquadMappingSuggestion {
    core::SquadMapping mapping;
    bool remembered = false; // from an earlier import of the same header set
};

// An earlier import of the same file (same hash and type).
struct PreviousImport {
    core::ImportId id;
    core::Timestamp timestamp{};
    bool undone = false;
};

struct SquadImportSummary {
    core::ImportId importId;
    int created = 0;
    int updated = 0;
    int skippedExisting = 0;
    int skippedAmbiguous = 0;
    int skippedExamples = 0; // the example row of the template
    int invalid = 0;
};

struct ImportUndoSummary {
    int deleted = 0;
    int archived = 0;    // created by the import, but something references them now
    int notReverted = 0; // players the import updated
};

// The import wizard (SPEC section 8, packet P16). Every method needs Import, otherwise
// PermissionDenied. Reading and validation run on the global thread pool with progress (AGENTS
// section 5); the players they compare with are read here, on the main thread, because the
// database connection lives there. Writes stay on the main thread in one Transaction.
class ImportService {
public:
    ImportService(data::DatabaseManager& database, data::ImportLogRepository& imports,
                  data::PlayerRepository& players, Session& session, const Clock& clock);

    // The future holds the errors of readImportFile(). A text file without a squad column and
    // without a steady column count has tableStatus UnsupportedFormat ("import_not_table").
    [[nodiscard]] core::Result<QFuture<core::Result<ImportSource>>>
    read(const QString& path, std::optional<int> sheet = std::nullopt) const;

    // Newest first.
    [[nodiscard]] core::Result<std::vector<PreviousImport>>
    previousImports(const ImportSource& source) const;

    // The mapping confirmed last time for the same header set, otherwise the automatic one.
    [[nodiscard]] core::Result<SquadMappingSuggestion>
    suggestSquadMapping(const ImportSource& source) const;

    // Progress: rows done of source.table.rows.size(). Needs a source with a table.
    [[nodiscard]] core::Result<QFuture<std::vector<core::SquadRow>>>
    validateSquad(core::TeamId team, const ImportSource& source,
                  const core::SquadMapping& mapping) const;

    // `rows` are the result of validateSquad() for the same source and mapping. First every check
    // that may refuse, writing nothing and making no backup: matching again against the current
    // squad (a changed row: Conflict "import_matches_changed", the rows marked MatchChanged, so the
    // report shows the new plan); invalid rows with InvalidRows::Abort: InvalidInput
    // ("import_has_errors"); no row to write: InvalidInput ("import_nothing"). Then the backup
    // (file databases) and one transaction, in which the matching is checked once more (a change
    // rolls back), then the players and import_log with the hash and the mapping. A backup or a
    // write that fails (a locked database: DatabaseLocked) writes nothing.
    [[nodiscard]] core::Result<SquadImportSummary>
    commitSquad(core::TeamId team, const ImportSource& source, const core::SquadMapping& mapping,
                std::span<core::SquadRow> rows, SquadImportOptions options);

    // Deletes the players the import created, or archives the ones something references (also a
    // saved lineup); updated players stay as they are (packet P16 decision 3). Backup first.
    // Undone already: Conflict ("import_undone"); not a squad import: UnsupportedFormat
    // ("import_type").
    [[nodiscard]] core::Result<ImportUndoSummary> undo(core::ImportId id);

private:
    [[nodiscard]] core::Status backup(std::string_view reason);
    // recheckSquadMatches() against the current squad; a changed row: Conflict
    // ("import_matches_changed").
    [[nodiscard]] core::Status recheck(core::TeamId team, const core::SquadMapping& mapping,
                                       std::span<core::SquadRow> rows) const;

    data::DatabaseManager& m_database;
    data::ImportLogRepository& m_imports;
    data::PlayerRepository& m_players;
    Session& m_session;
    const Clock& m_clock;
};

} // namespace fdss::services
