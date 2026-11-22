#pragma once

#include "core/domain/DateTypes.hpp"
#include "core/domain/Ids.hpp"
#include "core/parsing/MatchImport.hpp"
#include "core/parsing/SquadImport.hpp"
#include "core/parsing/TrainingImport.hpp"
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
class MatchLiveRepository;
class MatchRepository;
class MetricRepository;
class OpponentRepository;
class PlayerRepository;
class TrainingRepository;
} // namespace fdss::data

namespace fdss::services {

class Clock;
class Session;

// import_log.type of each import.
inline constexpr std::string_view kSquadImportType = "squad";
inline constexpr std::string_view kTrainingImportType = "training";
inline constexpr std::string_view kMatchImportType = "matches";

enum class ImportKind { Squad, Training, Matches };
[[nodiscard]] std::string_view importTypeOf(ImportKind kind) noexcept;

// The repositories of the import service.
struct ImportRepositories {
    data::ImportLogRepository& imports;
    data::PlayerRepository& players;
    data::TrainingRepository& training;
    data::MatchRepository& matches;
    data::MatchLiveRepository& matchLive;
    data::MetricRepository& metrics;
    data::OpponentRepository& opponents;
};

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

struct ActivityMappingSuggestion {
    core::ActivityMapping mapping;
    bool remembered = false; // from an earlier import of the same header set
};

// A training or match import (packet P18).
struct ActivityImportSummary {
    core::ImportId importId;
    int sessionsCreated = 0; // training
    int matchesCreated = 0;  // matches
    int created = 0;         // rows by action
    int updated = 0;
    int unchanged = 0;
    int kept = 0;
    int skipped = 0;
    int invalid = 0;
    int playersCreated = 0;
    int opponentsCreated = 0;
    int metricValues = 0;                // written
    core::NotImportedEvents notImported; // matches (decision 2)
};

// What an undo deletes beyond the rows the import wrote (packet P18 decision 10): data added or
// changed after the import in its sessions and matches. Any of them: the wizard asks first.
struct ImportUndoPreview {
    int notes = 0;
    int events = 0;
    int values = 0;  // metric values
    int entries = 0; // attendance, appearances, lineups, tactic changes, ratings changed by hand

    [[nodiscard]] bool any() const noexcept { return notes + events + values + entries > 0; }
    friend bool operator==(const ImportUndoPreview&, const ImportUndoPreview&) = default;
};

struct ImportUndoSummary {
    int deleted = 0;
    int archived = 0;    // created by the import, but something references them now
    int notReverted = 0; // players the import updated; rows changed since an activity import
    // Training and match imports.
    int sessionsDeleted = 0;
    int matchesDeleted = 0;
    int rowsDeleted = 0;  // attendance and appearances the import created in existing ones
    int rowsRestored = 0; // overwritten rows back to their values before
    int opponentsDeleted = 0;
};

// The import wizard (SPEC section 8, packet P16). Every method needs Import, otherwise
// PermissionDenied. Reading and validation run on the global thread pool with progress (AGENTS
// section 5); the players they compare with are read here, on the main thread, because the
// database connection lives there. Writes stay on the main thread in one Transaction.
class ImportService {
public:
    ImportService(data::DatabaseManager& database, ImportRepositories repositories,
                  Session& session, const Clock& clock);

    // The future holds the errors of readImportFile(). A text file without a column of the kind
    // and without a steady column count has tableStatus UnsupportedFormat ("import_not_table");
    // training with dates as headers is UnsupportedFormat ("import_wide_format", decision 7).
    [[nodiscard]] core::Result<QFuture<core::Result<ImportSource>>>
    read(const QString& path, std::optional<int> sheet = std::nullopt,
         ImportKind kind = ImportKind::Squad) const;

    // Imports of the same file and kind, newest first.
    [[nodiscard]] core::Result<std::vector<PreviousImport>>
    previousImports(const ImportSource& source, ImportKind kind = ImportKind::Squad) const;

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

    // --- Training and matches (packet P18) ---

    // The custom metrics a column of the kind may hold: scope training or match.
    [[nodiscard]] core::Result<std::vector<core::MetricDefinition>>
    metricsFor(ImportKind kind) const;
    // The mapping confirmed last time for the same header set, otherwise the automatic one.
    [[nodiscard]] core::Result<ActivityMappingSuggestion>
    suggestActivityMapping(ImportKind kind, const ImportSource& source) const;

    // Progress: rows done. The players and metrics are read here, on the main thread.
    [[nodiscard]] core::Result<QFuture<core::TrainingImport>>
    validateTraining(core::TeamId team, const ImportSource& source,
                     const core::ActivityMapping& mapping) const;
    [[nodiscard]] core::Result<QFuture<core::MatchImport>>
    validateMatches(core::TeamId team, const ImportSource& source,
                    const core::ActivityMapping& mapping) const;

    // The plan against the database now (synchronous: a few reads per session or match).
    [[nodiscard]] core::Result<core::TrainingPlan>
    planTraining(core::TeamId team, const core::TrainingImport& import,
                 const core::ActivityMapping& mapping, const core::ActivityOptions& options) const;
    [[nodiscard]] core::Result<core::MatchPlan>
    planMatches(core::TeamId team, const core::MatchImport& import,
                const core::ActivityMapping& mapping, const core::ActivityOptions& options) const;

    // `plan` is the plan the user saw. The same refusals as commitSquad(): a plan that differs
    // from a new one is Conflict ("import_matches_changed"), invalid rows with skipInvalid off
    // InvalidInput ("import_has_errors"), nothing to write InvalidInput ("import_nothing"); the
    // creation of players without a default position InvalidInput ("default_position"). Then the
    // backup and one transaction (the plan checked again), import_log with the rows written.
    [[nodiscard]] core::Result<ActivityImportSummary>
    commitTraining(core::TeamId team, const ImportSource& source,
                   const core::ActivityMapping& mapping, const core::TrainingImport& import,
                   const core::TrainingPlan& plan, const core::ActivityOptions& options);
    [[nodiscard]] core::Result<ActivityImportSummary>
    commitMatches(core::TeamId team, const ImportSource& source,
                  const core::ActivityMapping& mapping, const core::MatchImport& import,
                  const core::MatchPlan& plan, const core::ActivityOptions& options);

    // What undo(id) would delete beyond the import's own rows (decision 10). Empty for a squad
    // import.
    [[nodiscard]] core::Result<ImportUndoPreview> undoPreview(core::ImportId id) const;

    // Squad: deletes the players the import created, or archives the ones something references
    // (also a saved lineup); updated players stay as they are (packet P16 decision 3). Training
    // and matches (packet P18 decision 10): overwritten rows get their values back while they
    // hold the imported ones; the created rows, sessions and matches go with every child row,
    // then the created opponents no match refers to, then the created players as for the squad.
    // Backup first. Undone already: Conflict ("import_undone"); another type: UnsupportedFormat
    // ("import_type").
    [[nodiscard]] core::Result<ImportUndoSummary> undo(core::ImportId id);

private:
    [[nodiscard]] core::Status checkActivityCommit(const ActivityImportSummary& summary,
                                                   const core::ActivityOptions& options,
                                                   bool createsPlayers) const;
    [[nodiscard]] core::Result<std::vector<core::PlayerId>>
    createPlayers(core::TeamId team, std::span<const core::NewPlayer> players,
                  core::ImportId importId);
    [[nodiscard]] core::Result<core::ImportId> createLog(const ImportSource& source,
                                                         ImportKind kind,
                                                         const core::ActivityMapping& mapping,
                                                         const ActivityImportSummary& summary);
    // The created players of an import: deleted, or archived when referenced.
    [[nodiscard]] core::Status undoPlayers(core::ImportId id, ImportUndoSummary& summary);
    [[nodiscard]] core::Status backup(std::string_view reason);
    // recheckSquadMatches() against the current squad; a changed row: Conflict
    // ("import_matches_changed").
    [[nodiscard]] core::Status recheck(core::TeamId team, const core::SquadMapping& mapping,
                                       std::span<core::SquadRow> rows) const;

    data::DatabaseManager& m_database;
    data::ImportLogRepository& m_imports;
    data::PlayerRepository& m_players;
    ImportRepositories m_repositories;
    Session& m_session;
    const Clock& m_clock;
};

} // namespace fdss::services
