#include "data/repositories/ImportLogRepository.hpp"

#include "data/repositories/RepositorySupport.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <utility>

namespace fdss::data {

namespace {

const auto kSelectEntry = QStringLiteral(
    "SELECT id, timestamp, file, hash, type, rows_ok, rows_failed, report_json FROM import_log ");

// The row with report_json still as text: a row function cannot fail on its own.
struct RawEntry {
    ImportLogEntry entry;
    std::optional<std::string> reportJson;
};

RawEntry readRaw(RowReader& row) {
    RawEntry raw;
    raw.entry.id = row.id<core::ImportId>();
    raw.entry.timestamp = row.optionalTimestamp().value_or(core::Timestamp{});
    raw.entry.file = row.text();
    raw.entry.hash = row.text();
    raw.entry.type = row.text();
    raw.entry.rowsOk = row.integer();
    raw.entry.rowsFailed = row.integer();
    raw.reportJson = row.optionalText();
    return raw;
}

QString fromStd(const std::string& text) { return QString::fromStdString(text); }

QByteArray toJson(const ImportReport& report) {
    QJsonObject object;
    object.insert(QStringLiteral("created"), report.created);
    object.insert(QStringLiteral("updated"), report.updated);
    object.insert(QStringLiteral("skipped"), report.skipped);
    if (report.undoneAt) {
        object.insert(QStringLiteral("undoneAt"), toIsoTimestamp(*report.undoneAt));
    }
    if (report.mapping) {
        QJsonObject mapping;
        mapping.insert(QStringLiteral("headerKey"), fromStd(report.mapping->headerKey));
        QJsonArray columns;
        for (const std::string& column : report.mapping->columns) {
            columns.append(fromStd(column));
        }
        mapping.insert(QStringLiteral("columns"), columns);
        QJsonObject options;
        for (const auto& [key, value] : report.mapping->options) {
            options.insert(fromStd(key), fromStd(value));
        }
        mapping.insert(QStringLiteral("options"), options);
        object.insert(QStringLiteral("mapping"), mapping);
    }
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

core::Error damaged(const char* detail) {
    return core::Error{core::ErrorCode::Database, detail, {}};
}

core::Result<ImportReport> fromJson(const std::optional<std::string>& json) {
    ImportReport report;
    if (!json) {
        return report;
    }
    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray::fromStdString(*json), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return std::unexpected(damaged("import_log.report_json is not a JSON object"));
    }
    const QJsonObject object = document.object();
    report.created = object.value(QStringLiteral("created")).toInt();
    report.updated = object.value(QStringLiteral("updated")).toInt();
    report.skipped = object.value(QStringLiteral("skipped")).toInt();
    if (const QJsonValue undone = object.value(QStringLiteral("undoneAt")); undone.isString()) {
        auto timestamp = parseIsoTimestamp(undone.toString());
        if (!timestamp) {
            return std::unexpected(damaged("import_log.report_json has a bad undoneAt"));
        }
        report.undoneAt = *timestamp;
    }
    if (const QJsonValue mappingValue = object.value(QStringLiteral("mapping"));
        mappingValue.isObject()) {
        const QJsonObject mapping = mappingValue.toObject();
        ImportMappingRecord record;
        record.headerKey = mapping.value(QStringLiteral("headerKey")).toString().toStdString();
        for (const QJsonValue column : mapping.value(QStringLiteral("columns")).toArray()) {
            record.columns.push_back(column.toString().toStdString());
        }
        const QJsonObject options = mapping.value(QStringLiteral("options")).toObject();
        for (auto option = options.begin(); option != options.end(); ++option) {
            record.options.emplace(option.key().toStdString(),
                                   option.value().toString().toStdString());
        }
        report.mapping = std::move(record);
    }
    return report;
}

core::Result<ImportLogEntry> complete(RawEntry raw) {
    auto report = fromJson(raw.reportJson);
    if (!report) {
        return std::unexpected(report.error());
    }
    raw.entry.report = std::move(*report);
    return std::move(raw.entry);
}

core::Result<std::vector<ImportLogEntry>> completeAll(std::vector<RawEntry> raws) {
    std::vector<ImportLogEntry> entries;
    entries.reserve(raws.size());
    for (RawEntry& raw : raws) {
        auto entry = complete(std::move(raw));
        if (!entry) {
            return std::unexpected(entry.error());
        }
        entries.push_back(std::move(*entry));
    }
    return entries;
}

QVariant jsonValue(const ImportReport& report) { return QString::fromUtf8(toJson(report)); }

} // namespace

core::Result<core::ImportId> ImportLogRepository::create(const ImportLogDraft& draft) {
    const auto id =
        insertRow(m_db,
                  QStringLiteral("INSERT INTO import_log (timestamp, file, hash, type, "
                                 "rows_ok, rows_failed, report_json) "
                                 "VALUES (?, ?, ?, ?, ?, ?, ?)"),
                  {toIsoTimestamp(draft.timestamp), textValue(draft.file), textValue(draft.hash),
                   textValue(draft.type), draft.rowsOk, draft.rowsFailed, jsonValue(draft.report)},
                  "create import log");
    if (!id) {
        return std::unexpected(id.error());
    }
    return core::ImportId{*id};
}

core::Result<ImportLogEntry> ImportLogRepository::get(core::ImportId id) const {
    auto raw = selectOne(m_db, kSelectEntry + QStringLiteral("WHERE id = ?"), {idValue(id)},
                         "get import log", readRaw);
    if (!raw) {
        return std::unexpected(raw.error());
    }
    return complete(std::move(*raw));
}

core::Result<std::vector<ImportLogEntry>>
ImportLogRepository::listByHash(std::string_view hash, std::string_view type) const {
    auto raws = selectAll(m_db,
                          kSelectEntry + QStringLiteral("WHERE hash = ? AND type = ? "
                                                        "ORDER BY id DESC"),
                          {textValue(hash), textValue(type)}, "list imports by hash", readRaw);
    if (!raws) {
        return std::unexpected(raws.error());
    }
    return completeAll(std::move(*raws));
}

core::Result<std::optional<ImportMappingRecord>>
ImportLogRepository::latestMapping(std::string_view type, std::string_view headerKey) const {
    auto raws = selectAll(m_db,
                          kSelectEntry + QStringLiteral("WHERE type = ? AND report_json IS NOT "
                                                        "NULL ORDER BY id DESC"),
                          {textValue(type)}, "find import mapping", readRaw);
    if (!raws) {
        return std::unexpected(raws.error());
    }
    for (RawEntry& raw : *raws) {
        auto entry = complete(std::move(raw));
        if (!entry) {
            return std::unexpected(entry.error());
        }
        if (entry->report.mapping && entry->report.mapping->headerKey == headerKey) {
            return std::move(entry->report.mapping);
        }
    }
    return std::optional<ImportMappingRecord>{};
}

core::Status ImportLogRepository::setReport(core::ImportId id, const ImportReport& report) {
    return changeRow(m_db, QStringLiteral("UPDATE import_log SET report_json = ? WHERE id = ?"),
                     {jsonValue(report), idValue(id)}, "set import report");
}

} // namespace fdss::data
