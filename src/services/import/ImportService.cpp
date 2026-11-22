#include "services/import/ImportService.hpp"

#include "core/domain/Permissions.hpp"
#include "core/domain/Validation.hpp"
#include "core/parsing/HeaderMatching.hpp"
#include "core/parsing/ImportValues.hpp"
#include "data/db/DatabaseManager.hpp"
#include "data/db/Transaction.hpp"
#include "data/repositories/ImportLogRepository.hpp"
#include "data/repositories/MatchRepository.hpp"
#include "data/repositories/MetricRepository.hpp"
#include "data/repositories/OpponentRepository.hpp"
#include "data/repositories/PlayerRepository.hpp"
#include "data/repositories/TrainingRepository.hpp"
#include "services/Clock.hpp"
#include "services/ServiceSupport.hpp"
#include "services/Session.hpp"
#include "services/import/ActivityUndo.hpp"
#include "services/import/MatchImportWriter.hpp"
#include "services/import/TrainingImportWriter.hpp"

#include <QFileInfo>
#include <QPromise>
#include <QtConcurrentRun>

#include <algorithm>
#include <utility>

namespace fdss::services {

namespace {

constexpr auto kNameOrderOption = "name_order";
constexpr auto kDefaultPositionOption = "default_position";

core::Error inputError(const char* detail, const char* field) {
    return core::Error{core::ErrorCode::InvalidInput, detail, field};
}

data::ImportMappingRecord toRecord(const core::SourceTable& table,
                                   const core::SquadMapping& mapping) {
    data::ImportMappingRecord record;
    record.headerKey = core::headerKey(table.headers);
    for (const auto& field : mapping.columns) {
        record.columns.emplace_back(field ? core::toKey(*field) : std::string_view{});
    }
    record.options.emplace(kNameOrderOption, core::toKey(mapping.nameOrder));
    if (mapping.defaultPosition) {
        record.options.emplace(kDefaultPositionOption, core::toDbText(*mapping.defaultPosition));
    }
    return record;
}

// A record that does not fit the table (another column count) is not used.
std::optional<core::SquadMapping> fromRecord(const data::ImportMappingRecord& record,
                                             const core::SourceTable& table) {
    if (record.columns.size() != table.headers.size()) {
        return std::nullopt;
    }
    core::SquadMapping mapping;
    for (const std::string& key : record.columns) {
        mapping.columns.push_back(core::squadFieldFromKey(key));
    }
    if (const auto order = record.options.find(kNameOrderOption); order != record.options.end()) {
        mapping.nameOrder = core::nameOrderFromKey(order->second).value_or(mapping.nameOrder);
    }
    if (const auto position = record.options.find(kDefaultPositionOption);
        position != record.options.end()) {
        if (auto parsed = core::fromDbText<core::Position>(position->second)) {
            mapping.defaultPosition = *parsed;
        }
    }
    return mapping;
}

constexpr std::string_view kMetricPrefix = "metric:";

core::MetricScope scopeOf(ImportKind kind) {
    return kind == ImportKind::Matches ? core::MetricScope::Match : core::MetricScope::Training;
}

std::string fieldKey(ImportKind kind, int field) {
    if (kind == ImportKind::Matches) {
        return std::string(core::toKey(static_cast<core::MatchField>(field)));
    }
    return std::string(core::toKey(static_cast<core::TrainingField>(field)));
}

std::optional<int> fieldFromKey(ImportKind kind, std::string_view key) {
    if (kind == ImportKind::Matches) {
        const auto field = core::matchFieldFromKey(key);
        return field ? std::optional(static_cast<int>(*field)) : std::nullopt;
    }
    const auto field = core::trainingFieldFromKey(key);
    return field ? std::optional(static_cast<int>(*field)) : std::nullopt;
}

data::ImportMappingRecord toRecord(ImportKind kind, const core::SourceTable& table,
                                   const core::ActivityMapping& mapping,
                                   std::span<const core::MetricDefinition> metrics) {
    data::ImportMappingRecord record;
    record.headerKey = core::headerKey(table.headers);
    for (const core::ColumnTarget& target : mapping.columns) {
        std::string key;
        if (target.field) {
            key = fieldKey(kind, *target.field);
        } else if (target.metric) {
            const auto metric =
                std::ranges::find(metrics, *target.metric, &core::MetricDefinition::id);
            if (metric != metrics.end()) {
                key = std::string(kMetricPrefix) + metric->key;
            }
        }
        record.columns.push_back(std::move(key));
    }
    return record;
}

// A record that does not fit the table is not used; a metric that is gone leaves its column empty.
std::optional<core::ActivityMapping> fromRecord(ImportKind kind,
                                                const data::ImportMappingRecord& record,
                                                const core::SourceTable& table,
                                                std::span<const core::MetricDefinition> metrics) {
    if (record.columns.size() != table.headers.size()) {
        return std::nullopt;
    }
    core::ActivityMapping mapping;
    for (const std::string& key : record.columns) {
        core::ColumnTarget target;
        if (key.starts_with(kMetricPrefix)) {
            const std::string metricKey = key.substr(kMetricPrefix.size());
            const auto metric = std::ranges::find(metrics, metricKey, &core::MetricDefinition::key);
            if (metric != metrics.end()) {
                target.metric = metric->id;
            }
        } else {
            target.field = fieldFromKey(kind, key);
        }
        mapping.columns.push_back(target);
    }
    return mapping;
}

// Whether a text file is a table of the kind at all (P16 manual test, for every kind).
bool looksLikeTable(ImportKind kind, const core::SourceTable& table) {
    if (kind == ImportKind::Squad) {
        return core::looksLikeSquadTable(table);
    }
    const core::ActivityMapping mapping = kind == ImportKind::Training
                                              ? core::autoMapTraining(table.headers, {})
                                              : core::autoMapMatches(table.headers, {});
    if (std::ranges::any_of(mapping.columns, [](const core::ColumnTarget& target) {
            return target.field.has_value();
        })) {
        return true;
    }
    const std::size_t width = table.headers.size();
    if (width < 2 || table.rows.empty()) {
        return false;
    }
    const auto consistent = std::ranges::count_if(
        table.rows, [width](const core::SourceRow& row) { return row.cells.size() == width; });
    return static_cast<std::size_t>(consistent) * 2 > table.rows.size();
}

core::Error changedError() {
    return core::Error{core::ErrorCode::Conflict, "the database changed since the check",
                       "import_matches_changed"};
}

template <class Plan> void countActions(const Plan& plan, ActivityImportSummary& summary) {
    for (const auto& row : plan.rows) {
        switch (row.action) {
        case core::ActivityAction::Create:
            ++summary.created;
            break;
        case core::ActivityAction::Update:
            ++summary.updated;
            break;
        case core::ActivityAction::Unchanged:
            ++summary.unchanged;
            break;
        case core::ActivityAction::KeepExisting:
            ++summary.kept;
            break;
        case core::ActivityAction::Skip:
            ++summary.skipped;
            break;
        case core::ActivityAction::Invalid:
            ++summary.invalid;
            break;
        }
    }
}

} // namespace

std::string_view importTypeOf(ImportKind kind) noexcept {
    switch (kind) {
    case ImportKind::Squad:
        return kSquadImportType;
    case ImportKind::Training:
        return kTrainingImportType;
    case ImportKind::Matches:
        return kMatchImportType;
    }
    return kSquadImportType;
}

ImportService::ImportService(data::DatabaseManager& database, ImportRepositories repositories,
                             Session& session, const Clock& clock)
    : m_database(database), m_imports(repositories.imports), m_players(repositories.players),
      m_repositories(repositories), m_session(session), m_clock(clock) {}

core::Result<QFuture<core::Result<ImportSource>>>
ImportService::read(const QString& path, std::optional<int> sheet, ImportKind kind) const {
    if (auto allowed = m_session.require(core::Permission::Import); !allowed) {
        return std::unexpected(allowed.error());
    }
    return QtConcurrent::run([path, sheet, kind] {
        auto source = readImportFile(path, sheet);
        if (!source || !source->tableStatus) {
            return source;
        }
        // One column per date: the long format of the template is needed (P18 decision 7).
        if (kind != ImportKind::Squad && core::looksLikeWideFormat(source->table)) {
            source->tableStatus =
                std::unexpected(core::Error{core::ErrorCode::UnsupportedFormat,
                                            "dates as column headers", "import_wide_format"});
            return source;
        }
        // A text file that is no table (prose, a list, markdown) has nothing to map.
        if (source->kind == ImportFileKind::Csv && !looksLikeTable(kind, source->table)) {
            source->tableStatus = std::unexpected(core::Error{
                core::ErrorCode::UnsupportedFormat, "the text is not a table", "import_not_table"});
        }
        return source;
    });
}

core::Result<std::vector<PreviousImport>> ImportService::previousImports(const ImportSource& source,
                                                                         ImportKind kind) const {
    if (auto allowed = m_session.require(core::Permission::Import); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto entries = m_imports.listByHash(source.hash.toStdString(), importTypeOf(kind));
    if (!entries) {
        return std::unexpected(entries.error());
    }
    std::vector<PreviousImport> previous;
    previous.reserve(entries->size());
    for (const data::ImportLogEntry& entry : *entries) {
        previous.push_back(
            PreviousImport{entry.id, entry.timestamp, entry.report.undoneAt.has_value()});
    }
    return previous;
}

core::Result<SquadMappingSuggestion>
ImportService::suggestSquadMapping(const ImportSource& source) const {
    if (auto allowed = m_session.require(core::Permission::Import); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto record = m_imports.latestMapping(kSquadImportType, core::headerKey(source.table.headers));
    if (!record) {
        return std::unexpected(record.error());
    }
    if (*record) {
        if (auto mapping = fromRecord(**record, source.table)) {
            return SquadMappingSuggestion{std::move(*mapping), true};
        }
    }
    return SquadMappingSuggestion{core::autoMapSquad(source.table.headers), false};
}

core::Result<QFuture<std::vector<core::SquadRow>>>
ImportService::validateSquad(core::TeamId team, const ImportSource& source,
                             const core::SquadMapping& mapping) const {
    if (auto allowed = m_session.require(core::Permission::Import); !allowed) {
        return std::unexpected(allowed.error());
    }
    if (!source.tableStatus) {
        return std::unexpected(source.tableStatus.error());
    }
    auto players = m_players.list(team, data::ArchivedPlayers::Exclude);
    if (!players) {
        return std::unexpected(players.error());
    }
    const int currentYear = static_cast<int>(m_clock.today().year());
    return QtConcurrent::run([table = source.table, mapping, players = std::move(*players),
                              currentYear](QPromise<std::vector<core::SquadRow>>& promise) {
        promise.setProgressRange(0, static_cast<int>(table.rows.size()));
        auto rows =
            core::validateSquad(table, mapping, players, currentYear, [&promise](std::size_t done) {
                promise.setProgressValue(static_cast<int>(done));
            });
        promise.addResult(std::move(rows));
    });
}

core::Status ImportService::checkDatabaseFile() const {
    // SPEC section 8: a database file deleted (or moved) while the app runs. The open connection
    // would go on writing into the deleted file, so the import refuses instead.
    const QString path = m_database.filePath();
    if (!path.isEmpty() && !QFileInfo::exists(path)) {
        return std::unexpected(
            core::Error{core::ErrorCode::Io, "the database file is gone", "database_missing"});
    }
    return {};
}

core::Status ImportService::backup(std::string_view reason) {
    if (auto present = checkDatabaseFile(); !present) {
        return present;
    }
    // An in-memory database (tests, qml_smoke) has no file to copy.
    if (m_database.filePath().isEmpty()) {
        return {};
    }
    auto path = m_database.backup(reason);
    if (!path) {
        return std::unexpected(path.error());
    }
    return {};
}

core::Status ImportService::recheck(core::TeamId team, const core::SquadMapping& mapping,
                                    std::span<core::SquadRow> rows) const {
    const auto players = m_players.list(team, data::ArchivedPlayers::Exclude);
    if (!players) {
        return std::unexpected(players.error());
    }
    if (core::recheckSquadMatches(rows, mapping, *players) > 0) {
        return std::unexpected(core::Error{core::ErrorCode::Conflict,
                                           "the squad changed since the check",
                                           "import_matches_changed"});
    }
    return {};
}

core::Result<SquadImportSummary> ImportService::commitSquad(core::TeamId team,
                                                            const ImportSource& source,
                                                            const core::SquadMapping& mapping,
                                                            std::span<core::SquadRow> rows,
                                                            SquadImportOptions options) {
    if (auto allowed = m_session.require(core::Permission::Import); !allowed) {
        return std::unexpected(allowed.error());
    }
    if (!source.tableStatus) {
        return std::unexpected(source.tableStatus.error());
    }
    // The squad may have changed since the check: the user then sees the new plan first.
    if (auto changed = recheck(team, mapping, rows); !changed) {
        return std::unexpected(changed.error());
    }
    SquadImportSummary summary;
    for (const core::SquadRow& row : rows) {
        switch (core::actionFor(row, options.existing)) {
        case core::RowAction::Create:
            ++summary.created;
            break;
        case core::RowAction::Update:
            ++summary.updated;
            break;
        case core::RowAction::SkipExisting:
            ++summary.skippedExisting;
            break;
        case core::RowAction::SkipAmbiguous:
            ++summary.skippedAmbiguous;
            break;
        case core::RowAction::SkipExample:
            ++summary.skippedExamples;
            break;
        case core::RowAction::Invalid:
            ++summary.invalid;
            break;
        }
    }
    if (summary.invalid > 0 && options.invalid == InvalidRows::Abort) {
        return std::unexpected(inputError("rows with errors", "import_has_errors"));
    }
    if (summary.created + summary.updated == 0) {
        return std::unexpected(inputError("no row to write", "import_nothing"));
    }
    // Every refusal is behind us: back up directly before the write transaction.
    if (auto saved = backup("import"); !saved) {
        return std::unexpected(saved.error());
    }
    auto transaction = data::Transaction::begin(m_database);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    // Again inside the transaction, so nothing can change before the write; a change rolls back.
    if (auto changed = recheck(team, mapping, rows); !changed) {
        return std::unexpected(changed.error());
    }
    const int currentYear = static_cast<int>(m_clock.today().year());

    data::ImportLogDraft log;
    log.timestamp = m_clock.now();
    log.file = source.fileName.toStdString();
    log.hash = source.hash.toStdString();
    log.type = std::string(kSquadImportType);
    log.rowsOk = summary.created + summary.updated;
    log.rowsFailed = summary.invalid;
    log.report.mapping = toRecord(source.table, mapping);
    log.report.created = summary.created;
    log.report.updated = summary.updated;
    log.report.skipped =
        summary.skippedExisting + summary.skippedAmbiguous + summary.skippedExamples;
    auto importId = m_imports.create(log);
    if (!importId) {
        return std::unexpected(importId.error());
    }
    summary.importId = *importId;

    for (const core::SquadRow& row : rows) {
        const core::RowAction action = core::actionFor(row, options.existing);
        if (action == core::RowAction::Create) {
            if (auto valid = firstError(core::validate(row.draft, currentYear)); !valid) {
                return std::unexpected(valid.error());
            }
            if (auto created = m_players.create(team, row.draft, *importId); !created) {
                return std::unexpected(created.error());
            }
        } else if (action == core::RowAction::Update && row.existing) {
            auto stored = m_players.get(*row.existing);
            if (!stored) {
                return std::unexpected(stored.error());
            }
            const core::PlayerDraft merged = core::mergeUpdate(*stored, row);
            if (auto valid = firstError(core::validate(merged, currentYear)); !valid) {
                return std::unexpected(valid.error());
            }
            if (auto updated = m_players.update(*row.existing, merged); !updated) {
                return std::unexpected(updated.error());
            }
        }
    }
    if (auto present = checkDatabaseFile(); !present) {
        return std::unexpected(present.error());
    }
    if (auto committed = transaction->commit(); !committed) {
        return std::unexpected(committed.error());
    }
    return summary;
}

core::Status ImportService::undoPlayers(core::ImportId id, ImportUndoSummary& summary) {
    auto players = m_players.listByImport(id);
    if (!players) {
        return std::unexpected(players.error());
    }
    const core::Date today = m_clock.today();
    for (const core::Player& player : *players) {
        auto inLineup = m_players.isInLineup(player.id);
        if (!inLineup) {
            return std::unexpected(inLineup.error());
        }
        if (!*inLineup) {
            auto removed = m_players.remove(player.id);
            if (removed) {
                ++summary.deleted;
                continue;
            }
            // Conflict: another table references the player. Anything else is a real failure.
            if (removed.error().code != core::ErrorCode::Conflict) {
                return std::unexpected(removed.error());
            }
        }
        // Archived already (the squad screen), it stays as it is.
        if (player.archivedAt) {
            ++summary.archived;
            continue;
        }
        if (auto archived = m_players.archive(player.id, today); !archived) {
            return std::unexpected(archived.error());
        }
        ++summary.archived;
    }
    return {};
}

core::Result<ImportUndoPreview> ImportService::undoPreview(core::ImportId id) const {
    if (auto allowed = m_session.require(core::Permission::Import); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto entry = m_imports.get(id);
    if (!entry) {
        return std::unexpected(entry.error());
    }
    ImportUndoPreview preview;
    if (entry->type == kSquadImportType || entry->report.undoneAt) {
        return preview;
    }
    auto values = m_repositories.metrics.countAddedLater(id);
    if (!values) {
        return std::unexpected(values.error());
    }
    preview.values = *values;
    if (entry->type == kTrainingImportType) {
        auto attendance = m_repositories.training.countAttendanceAddedLater(id);
        if (!attendance) {
            return std::unexpected(attendance.error());
        }
        preview.entries += *attendance;
    } else if (entry->type == kMatchImportType) {
        auto additions = m_repositories.matches.countAddedLater(id);
        if (!additions) {
            return std::unexpected(additions.error());
        }
        preview.notes = additions->notes;
        preview.events = additions->events;
        preview.entries += additions->appearances + additions->lineups + additions->tacticChanges;
    }
    const ActivityRowStores stores{m_repositories.training, m_repositories.matches,
                                   m_repositories.metrics};
    if (auto changed = countChangedRows(entry->report, stores, preview); !changed) {
        return std::unexpected(changed.error());
    }
    return preview;
}

core::Result<ImportUndoSummary> ImportService::undo(core::ImportId id) {
    if (auto allowed = m_session.require(core::Permission::Import); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto entry = m_imports.get(id);
    if (!entry) {
        return std::unexpected(entry.error());
    }
    const bool squad = entry->type == kSquadImportType;
    const bool training = entry->type == kTrainingImportType;
    const bool matches = entry->type == kMatchImportType;
    if (!squad && !training && !matches) {
        return std::unexpected(core::Error{core::ErrorCode::UnsupportedFormat,
                                           "an unknown import type", "import_type"});
    }
    if (entry->report.undoneAt) {
        return std::unexpected(
            core::Error{core::ErrorCode::Conflict, "undone already", "import_undone"});
    }
    if (auto saved = backup("import_undo"); !saved) {
        return std::unexpected(saved.error());
    }
    auto transaction = data::Transaction::begin(m_database);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    ImportUndoSummary summary;
    if (squad) {
        summary.notReverted = entry->report.updated;
    } else {
        const ActivityRowStores stores{m_repositories.training, m_repositories.matches,
                                       m_repositories.metrics};
        if (auto restored = restoreOverwrittenRows(entry->report, stores, summary); !restored) {
            return std::unexpected(restored.error());
        }
        if (auto values = m_repositories.metrics.removeByImport(id); !values) {
            return std::unexpected(values.error());
        }
        if (training) {
            auto rows = m_repositories.training.removeAttendanceByImport(id);
            if (!rows) {
                return std::unexpected(rows.error());
            }
            auto sessions = m_repositories.training.removeByImport(id);
            if (!sessions) {
                return std::unexpected(sessions.error());
            }
            summary.rowsDeleted = *rows;
            summary.sessionsDeleted = *sessions;
        } else {
            auto rows = m_repositories.matches.removeAppearancesByImport(id);
            if (!rows) {
                return std::unexpected(rows.error());
            }
            auto removed = m_repositories.matches.removeByImport(id);
            if (!removed) {
                return std::unexpected(removed.error());
            }
            summary.rowsDeleted = *rows;
            summary.matchesDeleted = *removed;
            for (const core::OpponentId opponent : entry->report.createdOpponents) {
                auto gone = m_repositories.opponents.remove(opponent);
                if (gone) {
                    ++summary.opponentsDeleted;
                } else if (gone.error().code != core::ErrorCode::Conflict &&
                           gone.error().code != core::ErrorCode::NotFound) {
                    return std::unexpected(gone.error());
                }
            }
        }
    }
    if (auto players = undoPlayers(id, summary); !players) {
        return std::unexpected(players.error());
    }
    data::ImportReport report = entry->report;
    report.undoneAt = m_clock.now();
    if (auto stored = m_imports.setReport(id, report); !stored) {
        return std::unexpected(stored.error());
    }
    if (auto present = checkDatabaseFile(); !present) {
        return std::unexpected(present.error());
    }
    if (auto committed = transaction->commit(); !committed) {
        return std::unexpected(committed.error());
    }
    return summary;
}

// --- Training and matches (packet P18) ---

core::Result<std::vector<core::MetricDefinition>> ImportService::metricsFor(ImportKind kind) const {
    if (auto allowed = m_session.require(core::Permission::Import); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto definitions = m_repositories.metrics.listDefinitions();
    if (!definitions) {
        return std::unexpected(definitions.error());
    }
    std::erase_if(*definitions, [scope = scopeOf(kind)](const core::MetricDefinition& metric) {
        return metric.scope != scope;
    });
    return definitions;
}

core::Result<ActivityMappingSuggestion>
ImportService::suggestActivityMapping(ImportKind kind, const ImportSource& source) const {
    auto metrics = metricsFor(kind);
    if (!metrics) {
        return std::unexpected(metrics.error());
    }
    auto record =
        m_imports.latestMapping(importTypeOf(kind), core::headerKey(source.table.headers));
    if (!record) {
        return std::unexpected(record.error());
    }
    if (*record) {
        if (auto mapping = fromRecord(kind, **record, source.table, *metrics)) {
            return ActivityMappingSuggestion{std::move(*mapping), true};
        }
    }
    return ActivityMappingSuggestion{kind == ImportKind::Matches
                                         ? core::autoMapMatches(source.table.headers, *metrics)
                                         : core::autoMapTraining(source.table.headers, *metrics),
                                     false};
}

core::Result<QFuture<core::TrainingImport>>
ImportService::validateTraining(core::TeamId team, const ImportSource& source,
                                const core::ActivityMapping& mapping) const {
    if (!source.tableStatus) {
        return std::unexpected(source.tableStatus.error());
    }
    auto metrics = metricsFor(ImportKind::Training);
    if (!metrics) {
        return std::unexpected(metrics.error());
    }
    auto players = m_players.list(team, data::ArchivedPlayers::Exclude);
    if (!players) {
        return std::unexpected(players.error());
    }
    const bool excelSerial = source.kind == ImportFileKind::Xlsx;
    return QtConcurrent::run([table = source.table, mapping, players = std::move(*players),
                              metrics = std::move(*metrics),
                              excelSerial](QPromise<core::TrainingImport>& promise) {
        promise.setProgressRange(0, static_cast<int>(table.rows.size()));
        promise.addResult(core::validateTraining(
            table, mapping, players, metrics, excelSerial,
            [&promise](std::size_t done) { promise.setProgressValue(static_cast<int>(done)); }));
    });
}

core::Result<QFuture<core::MatchImport>>
ImportService::validateMatches(core::TeamId team, const ImportSource& source,
                               const core::ActivityMapping& mapping) const {
    if (!source.tableStatus) {
        return std::unexpected(source.tableStatus.error());
    }
    auto metrics = metricsFor(ImportKind::Matches);
    if (!metrics) {
        return std::unexpected(metrics.error());
    }
    auto players = m_players.list(team, data::ArchivedPlayers::Exclude);
    if (!players) {
        return std::unexpected(players.error());
    }
    const bool excelSerial = source.kind == ImportFileKind::Xlsx;
    return QtConcurrent::run([table = source.table, mapping, players = std::move(*players),
                              metrics = std::move(*metrics),
                              excelSerial](QPromise<core::MatchImport>& promise) {
        promise.setProgressRange(0, static_cast<int>(table.rows.size()));
        promise.addResult(core::validateMatches(
            table, mapping, players, metrics, excelSerial,
            [&promise](std::size_t done) { promise.setProgressValue(static_cast<int>(done)); }));
    });
}

core::Result<core::TrainingPlan>
ImportService::planTraining(core::TeamId team, const core::TrainingImport& import,
                            const core::ActivityMapping& mapping,
                            const core::ActivityOptions& options) const {
    if (auto allowed = m_session.require(core::Permission::Import); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto players = m_players.list(team, data::ArchivedPlayers::Exclude);
    if (!players) {
        return std::unexpected(players.error());
    }
    auto metrics = metricsFor(ImportKind::Training);
    if (!metrics) {
        return std::unexpected(metrics.error());
    }
    const TrainingImportWriter writer(m_repositories.training, m_repositories.metrics);
    auto existing = writer.snapshot(team, import, mapping);
    if (!existing) {
        return std::unexpected(existing.error());
    }
    return core::planTraining(import, *existing, *players, *metrics, options);
}

core::Result<core::MatchPlan>
ImportService::planMatches(core::TeamId team, const core::MatchImport& import,
                           const core::ActivityMapping& mapping,
                           const core::ActivityOptions& options) const {
    if (auto allowed = m_session.require(core::Permission::Import); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto players = m_players.list(team, data::ArchivedPlayers::Exclude);
    if (!players) {
        return std::unexpected(players.error());
    }
    const MatchImportWriter writer(m_repositories.matches, m_repositories.matchLive,
                                   m_repositories.metrics, m_repositories.opponents);
    auto snapshot = writer.snapshot(team, import, mapping);
    if (!snapshot) {
        return std::unexpected(snapshot.error());
    }
    auto metrics = metricsFor(ImportKind::Matches);
    if (!metrics) {
        return std::unexpected(metrics.error());
    }
    return core::planMatches(import, snapshot->opponents, snapshot->matches, *players, *metrics,
                             options);
}

core::Status ImportService::checkActivityCommit(const ActivityImportSummary& summary,
                                                const core::ActivityOptions& options) const {
    if (summary.invalid > 0 && !options.skipInvalid) {
        return std::unexpected(inputError("rows with errors", "import_has_errors"));
    }
    if (summary.created + summary.updated == 0) {
        return std::unexpected(inputError("no row to write", "import_nothing"));
    }
    return {};
}

core::Result<std::vector<core::PlayerId>>
ImportService::createPlayers(core::TeamId team, std::span<const core::NewPlayer> players,
                             core::ImportId importId) {
    const int currentYear = static_cast<int>(m_clock.today().year());
    std::vector<core::PlayerId> ids;
    ids.reserve(players.size());
    for (const core::NewPlayer& player : players) {
        if (auto valid = firstError(core::validate(player.draft, currentYear)); !valid) {
            return std::unexpected(valid.error());
        }
        auto created = m_players.create(team, player.draft, importId);
        if (!created) {
            return std::unexpected(created.error());
        }
        ids.push_back(*created);
    }
    return ids;
}

core::Result<core::ImportId> ImportService::createLog(const ImportSource& source, ImportKind kind,
                                                      const core::ActivityMapping& mapping,
                                                      const ActivityImportSummary& summary) {
    auto metrics = m_repositories.metrics.listDefinitions();
    if (!metrics) {
        return std::unexpected(metrics.error());
    }
    data::ImportLogDraft log;
    log.timestamp = m_clock.now();
    log.file = source.fileName.toStdString();
    log.hash = source.hash.toStdString();
    log.type = std::string(importTypeOf(kind));
    log.rowsOk = summary.created + summary.updated;
    log.rowsFailed = summary.invalid;
    log.report.mapping = toRecord(kind, source.table, mapping, *metrics);
    log.report.created = summary.created;
    log.report.updated = summary.updated;
    log.report.skipped = summary.skipped + summary.unchanged + summary.kept;
    return m_imports.create(log);
}

namespace {

// The counts of an activity import in report_json, for the summary of the import list.
void storeCounts(data::ImportReport& report, const ActivityImportSummary& summary) {
    report.counts["sessions_created"] = summary.sessionsCreated;
    report.counts["matches_created"] = summary.matchesCreated;
    report.counts["players_created"] = summary.playersCreated;
    report.counts["opponents_created"] = summary.opponentsCreated;
    report.counts["metric_values"] = summary.metricValues;
    report.counts["goals_not_imported"] = summary.notImported.goals;
    report.counts["assists_not_imported"] = summary.notImported.assists;
    report.counts["cards_not_imported"] = summary.notImported.cards;
    std::erase_if(report.counts, [](const auto& count) { return count.second == 0; });
}

} // namespace

core::Result<ActivityImportSummary>
ImportService::commitTraining(core::TeamId team, const ImportSource& source,
                              const core::ActivityMapping& mapping,
                              const core::TrainingImport& import, const core::TrainingPlan& plan,
                              const core::ActivityOptions& options) {
    if (!source.tableStatus) {
        return std::unexpected(source.tableStatus.error());
    }
    // The database may have changed since the check: the user then sees the new plan first.
    auto fresh = planTraining(team, import, mapping, options);
    if (!fresh) {
        return std::unexpected(fresh.error());
    }
    if (*fresh != plan) {
        return std::unexpected(changedError());
    }
    ActivityImportSummary summary;
    countActions(plan, summary);
    summary.playersCreated = static_cast<int>(plan.newPlayers.size());
    if (auto allowed = checkActivityCommit(summary, options); !allowed) {
        return std::unexpected(allowed.error());
    }
    if (auto saved = backup("import"); !saved) {
        return std::unexpected(saved.error());
    }
    auto transaction = data::Transaction::begin(m_database);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    // Again inside the transaction, so nothing can change before the write.
    if (auto again = planTraining(team, import, mapping, options); !again || *again != plan) {
        return std::unexpected(again ? changedError() : again.error());
    }
    auto importId = createLog(source, ImportKind::Training, mapping, summary);
    if (!importId) {
        return std::unexpected(importId.error());
    }
    summary.importId = *importId;
    auto players = createPlayers(team, plan.newPlayers, *importId);
    if (!players) {
        return std::unexpected(players.error());
    }
    auto entry = m_imports.get(*importId);
    if (!entry) {
        return std::unexpected(entry.error());
    }
    data::ImportReport report = entry->report;
    TrainingImportWriter writer(m_repositories.training, m_repositories.metrics);
    if (auto written = writer.write(team, plan, *players, *importId, report, summary); !written) {
        return std::unexpected(written.error());
    }
    storeCounts(report, summary);
    if (auto stored = m_imports.setReport(*importId, report); !stored) {
        return std::unexpected(stored.error());
    }
    if (auto present = checkDatabaseFile(); !present) {
        return std::unexpected(present.error());
    }
    if (auto committed = transaction->commit(); !committed) {
        return std::unexpected(committed.error());
    }
    return summary;
}

core::Result<ActivityImportSummary>
ImportService::commitMatches(core::TeamId team, const ImportSource& source,
                             const core::ActivityMapping& mapping, const core::MatchImport& import,
                             const core::MatchPlan& plan, const core::ActivityOptions& options) {
    if (!source.tableStatus) {
        return std::unexpected(source.tableStatus.error());
    }
    auto fresh = planMatches(team, import, mapping, options);
    if (!fresh) {
        return std::unexpected(fresh.error());
    }
    if (*fresh != plan) {
        return std::unexpected(changedError());
    }
    ActivityImportSummary summary;
    countActions(plan, summary);
    summary.playersCreated = static_cast<int>(plan.newPlayers.size());
    summary.notImported = core::notImportedEvents(import, plan);
    if (auto allowed = checkActivityCommit(summary, options); !allowed) {
        return std::unexpected(allowed.error());
    }
    if (auto saved = backup("import"); !saved) {
        return std::unexpected(saved.error());
    }
    auto transaction = data::Transaction::begin(m_database);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    if (auto again = planMatches(team, import, mapping, options); !again || *again != plan) {
        return std::unexpected(again ? changedError() : again.error());
    }
    auto importId = createLog(source, ImportKind::Matches, mapping, summary);
    if (!importId) {
        return std::unexpected(importId.error());
    }
    summary.importId = *importId;
    auto players = createPlayers(team, plan.newPlayers, *importId);
    if (!players) {
        return std::unexpected(players.error());
    }
    auto entry = m_imports.get(*importId);
    if (!entry) {
        return std::unexpected(entry.error());
    }
    data::ImportReport report = entry->report;
    MatchImportWriter writer(m_repositories.matches, m_repositories.matchLive,
                             m_repositories.metrics, m_repositories.opponents);
    if (auto written = writer.write(team, plan, *players, *importId, report, summary); !written) {
        return std::unexpected(written.error());
    }
    storeCounts(report, summary);
    if (auto stored = m_imports.setReport(*importId, report); !stored) {
        return std::unexpected(stored.error());
    }
    if (auto present = checkDatabaseFile(); !present) {
        return std::unexpected(present.error());
    }
    if (auto committed = transaction->commit(); !committed) {
        return std::unexpected(committed.error());
    }
    return summary;
}

} // namespace fdss::services
