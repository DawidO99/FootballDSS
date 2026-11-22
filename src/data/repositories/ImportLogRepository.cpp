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

QJsonValue jsonOf(const std::optional<int>& value) {
    return value ? QJsonValue(*value) : QJsonValue(QJsonValue::Null);
}

std::optional<int> intOf(const QJsonValue& value) {
    return value.isDouble() ? std::optional(value.toInt()) : std::nullopt;
}

QJsonObject toJson(const core::AttendanceDraft& draft) {
    QJsonObject object;
    object.insert(QStringLiteral("p"), static_cast<qint64>(draft.playerId.value));
    object.insert(QStringLiteral("st"), fromStd(std::string(core::toDbText(draft.status))));
    object.insert(QStringLiteral("rpe"), jsonOf(draft.rpe));
    object.insert(QStringLiteral("min"), jsonOf(draft.minutesOverride));
    return object;
}

core::Result<core::AttendanceDraft> attendanceOf(const QJsonObject& object) {
    core::AttendanceDraft draft;
    draft.playerId = core::PlayerId{object.value(QStringLiteral("p")).toInteger()};
    auto status = core::fromDbText<core::AttendanceStatus>(
        object.value(QStringLiteral("st")).toString().toStdString());
    if (!status) {
        return std::unexpected(status.error());
    }
    draft.status = *status;
    draft.rpe = intOf(object.value(QStringLiteral("rpe")));
    draft.minutesOverride = intOf(object.value(QStringLiteral("min")));
    return draft;
}

QJsonObject toJson(const core::AppearanceDraft& draft) {
    QJsonObject object;
    object.insert(QStringLiteral("p"), static_cast<qint64>(draft.playerId.value));
    object.insert(QStringLiteral("st"), draft.started);
    object.insert(QStringLiteral("on"), jsonOf(draft.minuteOn));
    object.insert(QStringLiteral("off"), jsonOf(draft.minuteOff));
    object.insert(QStringLiteral("pos"),
                  draft.position ? QJsonValue(fromStd(std::string(core::toDbText(*draft.position))))
                                 : QJsonValue(QJsonValue::Null));
    object.insert(QStringLiteral("rt"), jsonOf(draft.rating));
    object.insert(QStringLiteral("rpe"), jsonOf(draft.rpe));
    return object;
}

core::Result<core::AppearanceDraft> appearanceOf(const QJsonObject& object) {
    core::AppearanceDraft draft;
    draft.playerId = core::PlayerId{object.value(QStringLiteral("p")).toInteger()};
    draft.started = object.value(QStringLiteral("st")).toBool();
    draft.minuteOn = intOf(object.value(QStringLiteral("on")));
    draft.minuteOff = intOf(object.value(QStringLiteral("off")));
    if (const QJsonValue position = object.value(QStringLiteral("pos")); position.isString()) {
        auto parsed = core::fromDbText<core::Position>(position.toString().toStdString());
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        draft.position = *parsed;
    }
    draft.rating = intOf(object.value(QStringLiteral("rt")));
    draft.rpe = intOf(object.value(QStringLiteral("rpe")));
    return draft;
}

QJsonValue jsonOf(const std::optional<core::TimeOfDay>& time) {
    return time ? QJsonValue(toIsoTime(*time)) : QJsonValue(QJsonValue::Null);
}

QJsonValue jsonOf(const std::optional<std::string>& text) {
    return text ? QJsonValue(fromStd(*text)) : QJsonValue(QJsonValue::Null);
}

core::Result<std::optional<core::TimeOfDay>> timeOf(const QJsonValue& value) {
    if (!value.isString()) {
        return std::optional<core::TimeOfDay>{};
    }
    auto time = parseIsoTime(value.toString());
    if (!time) {
        return std::unexpected(time.error());
    }
    return std::optional(*time);
}

std::optional<std::string> textOf(const QJsonValue& value) {
    return value.isString() ? std::optional(value.toString().toStdString()) : std::nullopt;
}

void writeCreated(QJsonObject& object, const ImportReport& report) {
    QJsonArray sessions;
    for (const ImportedSession& session : report.sessions) {
        QJsonObject item;
        item.insert(QStringLiteral("id"), static_cast<qint64>(session.id.value));
        item.insert(QStringLiteral("date"), toIsoDate(session.written.date));
        item.insert(QStringLiteral("start"), jsonOf(session.written.startTime));
        item.insert(QStringLiteral("min"), session.written.durationMin);
        item.insert(QStringLiteral("type"),
                    fromStd(std::string(core::toDbText(session.written.type))));
        item.insert(QStringLiteral("note"), jsonOf(session.written.note));
        sessions.append(item);
    }
    QJsonArray matches;
    for (const ImportedMatch& match : report.matches) {
        QJsonObject item;
        item.insert(QStringLiteral("id"), static_cast<qint64>(match.id.value));
        item.insert(QStringLiteral("opp"), static_cast<qint64>(match.written.opponentId.value));
        item.insert(QStringLiteral("date"), toIsoDate(match.written.date));
        item.insert(QStringLiteral("kick"), jsonOf(match.written.kickoffTime));
        item.insert(QStringLiteral("venue"),
                    fromStd(std::string(core::toDbText(match.written.venue))));
        item.insert(QStringLiteral("comp"), jsonOf(match.written.competition));
        item.insert(QStringLiteral("gf"), jsonOf(match.goalsFor));
        item.insert(QStringLiteral("ga"), jsonOf(match.goalsAgainst));
        matches.append(item);
    }
    if (!sessions.isEmpty()) {
        object.insert(QStringLiteral("sessions"), sessions);
    }
    if (!matches.isEmpty()) {
        object.insert(QStringLiteral("matches"), matches);
    }
}

core::Status readCreated(const QJsonObject& object, ImportReport& report) {
    for (const QJsonValue value : object.value(QStringLiteral("sessions")).toArray()) {
        const QJsonObject item = value.toObject();
        ImportedSession session;
        session.id = core::TrainingSessionId{item.value(QStringLiteral("id")).toInteger()};
        auto date = parseIsoDate(item.value(QStringLiteral("date")).toString());
        auto start = timeOf(item.value(QStringLiteral("start")));
        auto type = core::fromDbText<core::SessionType>(
            item.value(QStringLiteral("type")).toString().toStdString());
        if (!date || !start || !type) {
            return std::unexpected(
                core::Error{core::ErrorCode::Database, "a bad imported session", {}});
        }
        session.written.date = *date;
        session.written.startTime = *start;
        session.written.durationMin = item.value(QStringLiteral("min")).toInt();
        session.written.type = *type;
        session.written.note = textOf(item.value(QStringLiteral("note")));
        report.sessions.push_back(std::move(session));
    }
    for (const QJsonValue value : object.value(QStringLiteral("matches")).toArray()) {
        const QJsonObject item = value.toObject();
        ImportedMatch match;
        match.id = core::MatchId{item.value(QStringLiteral("id")).toInteger()};
        match.written.opponentId = core::OpponentId{item.value(QStringLiteral("opp")).toInteger()};
        auto date = parseIsoDate(item.value(QStringLiteral("date")).toString());
        auto kickoff = timeOf(item.value(QStringLiteral("kick")));
        auto venue = core::fromDbText<core::Venue>(
            item.value(QStringLiteral("venue")).toString().toStdString());
        if (!date || !kickoff || !venue) {
            return std::unexpected(
                core::Error{core::ErrorCode::Database, "a bad imported match", {}});
        }
        match.written.date = *date;
        match.written.kickoffTime = *kickoff;
        match.written.venue = *venue;
        match.written.competition = textOf(item.value(QStringLiteral("comp")));
        match.goalsFor = intOf(item.value(QStringLiteral("gf")));
        match.goalsAgainst = intOf(item.value(QStringLiteral("ga")));
        report.matches.push_back(std::move(match));
    }
    return {};
}

void writeRows(QJsonObject& object, const ImportReport& report) {
    writeCreated(object, report);
    QJsonArray attendance;
    for (const ImportedAttendance& row : report.attendance) {
        QJsonObject item;
        item.insert(QStringLiteral("s"), static_cast<qint64>(row.session.value));
        item.insert(QStringLiteral("w"), toJson(row.written));
        if (row.previous) {
            item.insert(QStringLiteral("b"), toJson(*row.previous));
        }
        attendance.append(item);
    }
    QJsonArray appearances;
    for (const ImportedAppearance& row : report.appearances) {
        QJsonObject item;
        item.insert(QStringLiteral("m"), static_cast<qint64>(row.match.value));
        item.insert(QStringLiteral("w"), toJson(row.written));
        if (row.previous) {
            item.insert(QStringLiteral("b"), toJson(*row.previous));
        }
        appearances.append(item);
    }
    QJsonArray values;
    for (const ImportedMetricValue& row : report.metricValues) {
        QJsonObject item;
        item.insert(QStringLiteral("d"), static_cast<qint64>(row.metric.value));
        if (row.session) {
            item.insert(QStringLiteral("s"), static_cast<qint64>(row.session->value));
        }
        if (row.match) {
            item.insert(QStringLiteral("m"), static_cast<qint64>(row.match->value));
        }
        item.insert(QStringLiteral("p"), static_cast<qint64>(row.player.value));
        item.insert(QStringLiteral("w"), row.written);
        if (row.previous) {
            item.insert(QStringLiteral("b"), *row.previous);
        }
        values.append(item);
    }
    QJsonArray opponents;
    for (const core::OpponentId id : report.createdOpponents) {
        opponents.append(static_cast<qint64>(id.value));
    }
    QJsonObject counts;
    for (const auto& [key, value] : report.counts) {
        counts.insert(fromStd(key), value);
    }
    if (!attendance.isEmpty()) {
        object.insert(QStringLiteral("attendance"), attendance);
    }
    if (!appearances.isEmpty()) {
        object.insert(QStringLiteral("appearances"), appearances);
    }
    if (!values.isEmpty()) {
        object.insert(QStringLiteral("metricValues"), values);
    }
    if (!opponents.isEmpty()) {
        object.insert(QStringLiteral("createdOpponents"), opponents);
    }
    if (!counts.isEmpty()) {
        object.insert(QStringLiteral("counts"), counts);
    }
}

core::Status readRows(const QJsonObject& object, ImportReport& report) {
    if (auto created = readCreated(object, report); !created) {
        return created;
    }
    for (const QJsonValue value : object.value(QStringLiteral("attendance")).toArray()) {
        const QJsonObject item = value.toObject();
        ImportedAttendance row;
        row.session = core::TrainingSessionId{item.value(QStringLiteral("s")).toInteger()};
        auto written = attendanceOf(item.value(QStringLiteral("w")).toObject());
        if (!written) {
            return std::unexpected(written.error());
        }
        row.written = *written;
        if (item.contains(QStringLiteral("b"))) {
            auto previous = attendanceOf(item.value(QStringLiteral("b")).toObject());
            if (!previous) {
                return std::unexpected(previous.error());
            }
            row.previous = *previous;
        }
        report.attendance.push_back(row);
    }
    for (const QJsonValue value : object.value(QStringLiteral("appearances")).toArray()) {
        const QJsonObject item = value.toObject();
        ImportedAppearance row;
        row.match = core::MatchId{item.value(QStringLiteral("m")).toInteger()};
        auto written = appearanceOf(item.value(QStringLiteral("w")).toObject());
        if (!written) {
            return std::unexpected(written.error());
        }
        row.written = *written;
        if (item.contains(QStringLiteral("b"))) {
            auto previous = appearanceOf(item.value(QStringLiteral("b")).toObject());
            if (!previous) {
                return std::unexpected(previous.error());
            }
            row.previous = *previous;
        }
        report.appearances.push_back(row);
    }
    for (const QJsonValue value : object.value(QStringLiteral("metricValues")).toArray()) {
        const QJsonObject item = value.toObject();
        ImportedMetricValue row;
        row.metric = core::MetricDefinitionId{item.value(QStringLiteral("d")).toInteger()};
        if (item.contains(QStringLiteral("s"))) {
            row.session = core::TrainingSessionId{item.value(QStringLiteral("s")).toInteger()};
        }
        if (item.contains(QStringLiteral("m"))) {
            row.match = core::MatchId{item.value(QStringLiteral("m")).toInteger()};
        }
        row.player = core::PlayerId{item.value(QStringLiteral("p")).toInteger()};
        row.written = item.value(QStringLiteral("w")).toDouble();
        if (item.contains(QStringLiteral("b"))) {
            row.previous = item.value(QStringLiteral("b")).toDouble();
        }
        report.metricValues.push_back(row);
    }
    for (const QJsonValue value : object.value(QStringLiteral("createdOpponents")).toArray()) {
        report.createdOpponents.push_back(core::OpponentId{value.toInteger()});
    }
    const QJsonObject counts = object.value(QStringLiteral("counts")).toObject();
    for (auto count = counts.begin(); count != counts.end(); ++count) {
        report.counts.emplace(count.key().toStdString(), count.value().toInt());
    }
    return {};
}

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
    writeRows(object, report);
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
    if (!readRows(object, report)) {
        return std::unexpected(damaged("import_log.report_json has bad imported rows"));
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
