#include "services/import/ImportService.hpp"

#include "core/domain/Permissions.hpp"
#include "core/domain/Validation.hpp"
#include "core/parsing/HeaderMatching.hpp"
#include "data/db/DatabaseManager.hpp"
#include "data/db/Transaction.hpp"
#include "data/repositories/ImportLogRepository.hpp"
#include "data/repositories/PlayerRepository.hpp"
#include "services/Clock.hpp"
#include "services/ServiceSupport.hpp"
#include "services/Session.hpp"

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

} // namespace

ImportService::ImportService(data::DatabaseManager& database, data::ImportLogRepository& imports,
                             data::PlayerRepository& players, Session& session, const Clock& clock)
    : m_database(database), m_imports(imports), m_players(players), m_session(session),
      m_clock(clock) {}

core::Result<QFuture<core::Result<ImportSource>>>
ImportService::read(const QString& path, std::optional<int> sheet) const {
    if (auto allowed = m_session.require(core::Permission::Import); !allowed) {
        return std::unexpected(allowed.error());
    }
    return QtConcurrent::run([path, sheet] { return readImportFile(path, sheet); });
}

core::Result<std::vector<PreviousImport>>
ImportService::previousImports(const ImportSource& source) const {
    if (auto allowed = m_session.require(core::Permission::Import); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto entries = m_imports.listByHash(source.hash.toStdString(), kSquadImportType);
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

core::Status ImportService::backup(std::string_view reason) {
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
    log.report.skipped = summary.skippedExisting + summary.skippedAmbiguous;
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
    if (auto committed = transaction->commit(); !committed) {
        return std::unexpected(committed.error());
    }
    return summary;
}

core::Result<ImportUndoSummary> ImportService::undo(core::ImportId id) {
    if (auto allowed = m_session.require(core::Permission::Import); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto entry = m_imports.get(id);
    if (!entry) {
        return std::unexpected(entry.error());
    }
    if (entry->type != kSquadImportType) {
        return std::unexpected(
            core::Error{core::ErrorCode::UnsupportedFormat, "not a squad import", "import_type"});
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
    auto players = m_players.listByImport(id);
    if (!players) {
        return std::unexpected(players.error());
    }
    ImportUndoSummary summary;
    summary.notReverted = entry->report.updated;
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
        if (auto archived = m_players.archive(player.id, today); !archived) {
            return std::unexpected(archived.error());
        }
        ++summary.archived;
    }
    data::ImportReport report = entry->report;
    report.undoneAt = m_clock.now();
    if (auto stored = m_imports.setReport(id, report); !stored) {
        return std::unexpected(stored.error());
    }
    if (auto committed = transaction->commit(); !committed) {
        return std::unexpected(committed.error());
    }
    return summary;
}

} // namespace fdss::services
