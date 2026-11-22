#include "data/repositories/RepositorySupport.hpp"

#include "core/domain/Validation.hpp"

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QSqlRecord>
#include <QTime>
#include <QTimeZone>

#include <chrono>
#include <format>
#include <limits>

namespace fdss::data {

namespace {

// SQLite extended result codes (sqlite3.h is not part of the Qt API). The driver turns them on.
constexpr int kConstraintCheck = 275;
constexpr int kConstraintForeignKey = 787;
constexpr int kConstraintNotNull = 1299;
constexpr int kConstraintPrimaryKey = 1555;
// ON DELETE RESTRICT reports this code instead of the foreign key one. The migrations cannot hold
// triggers of their own (see Migrations.hpp), so here it always means RESTRICT.
constexpr int kConstraintRestrict = 1811;
constexpr int kConstraintUnique = 2067;
constexpr int kConstraintDataType = 3091;

constexpr qsizetype kIsoDateLength = 10;
constexpr qsizetype kIsoTimeLength = 5;
constexpr int kMinutesPerHour = 60;

// "UNIQUE constraint failed: player.team_id, player.shirt_number" gives "shirt_number".
std::string lastColumnNamedIn(const QString& message) {
    return message.section(u'.', -1).trimmed().toStdString();
}

core::Error damagedValue(std::string_view what, const QString& text) {
    return core::Error{core::ErrorCode::Database,
                       std::format("stored {} '{}' cannot be read", what, text.toStdString()),
                       {}};
}

} // namespace

core::Error statementError(DatabaseManager& db, const QSqlError& error, std::string_view context,
                           ForeignKeyFailure foreignKeyFailure) {
    core::Error result = db.sqlError(error, context);
    switch (error.nativeErrorCode().toInt()) {
    case kConstraintUnique:
    case kConstraintPrimaryKey:
        result.code = core::ErrorCode::Conflict;
        result.field = lastColumnNamedIn(error.databaseText());
        break;
    case kConstraintForeignKey:
    case kConstraintRestrict:
        result.code = foreignKeyFailure == ForeignKeyFailure::RowInUse
                          ? core::ErrorCode::Conflict
                          : core::ErrorCode::InvalidInput;
        break;
    case kConstraintNotNull:
        result.code = core::ErrorCode::InvalidInput;
        result.field = lastColumnNamedIn(error.databaseText());
        break;
    case kConstraintCheck:
    case kConstraintDataType:
        result.code = core::ErrorCode::InvalidInput;
        break;
    default:
        break;
    }
    return result;
}

core::Result<QSqlQuery> runSql(DatabaseManager& db, const QString& sql, const QVariantList& values,
                               std::string_view context, ForeignKeyFailure foreignKeyFailure) {
    // After a lost transaction this statement would run on its own and commit at once.
    if (db.isTransactionBroken()) {
        return std::unexpected(core::Error{
            core::ErrorCode::Database,
            std::format("{}: an earlier error may have rolled back the transaction", context),
            {}});
    }
    QSqlQuery query(db.database());
    query.setForwardOnly(true);
    if (!query.prepare(sql)) {
        return std::unexpected(statementError(db, query.lastError(), context, foreignKeyFailure));
    }
    for (qsizetype i = 0; i < values.size(); ++i) {
        query.bindValue(static_cast<int>(i), values.at(i));
    }
    if (!query.exec()) {
        return std::unexpected(statementError(db, query.lastError(), context, foreignKeyFailure));
    }
    return query;
}

core::Result<std::int64_t> insertRow(DatabaseManager& db, const QString& sql,
                                     const QVariantList& values, std::string_view context) {
    const auto query = runSql(db, sql, values, context);
    if (!query) {
        return std::unexpected(query.error());
    }
    return query->lastInsertId().toLongLong();
}

core::Error rowNotFound(std::string_view context) {
    return core::Error{core::ErrorCode::NotFound, std::format("{}: no such row", context), {}};
}

core::Status changeRow(DatabaseManager& db, const QString& sql, const QVariantList& values,
                       std::string_view context, ForeignKeyFailure foreignKeyFailure) {
    const auto query = runSql(db, sql, values, context, foreignKeyFailure);
    if (!query) {
        return std::unexpected(query.error());
    }
    if (query->numRowsAffected() == 0) {
        return std::unexpected(rowNotFound(context));
    }
    return {};
}

core::Status requireRow(DatabaseManager& db, const QString& sql, const QVariantList& values,
                        std::string_view context) {
    auto query = runSql(db, sql, values, context);
    if (!query) {
        return std::unexpected(query.error());
    }
    if (query->next()) {
        return {};
    }
    if (query->lastError().isValid()) {
        return std::unexpected(statementError(db, query->lastError(), context));
    }
    return std::unexpected(rowNotFound(context));
}

core::Status checkSupportedDate(core::Date date, std::string_view field) {
    if (core::isSupportedDate(date)) {
        return {};
    }
    return std::unexpected(core::Error{
        core::ErrorCode::InvalidInput,
        std::format("date {} is outside the supported range", toIsoDate(date).toStdString()),
        std::string(field)});
}

QString toIsoDate(core::Date date) {
    return QString::fromStdString(std::format("{:04}-{:02}-{:02}", static_cast<int>(date.year()),
                                              static_cast<unsigned>(date.month()),
                                              static_cast<unsigned>(date.day())));
}

core::Result<core::Date> parseIsoDate(const QString& text) {
    const QDate date = QDate::fromString(text, QStringLiteral("yyyy-MM-dd"));
    if (text.size() != kIsoDateLength || !date.isValid()) {
        return std::unexpected(damagedValue("date", text));
    }
    return core::Date{std::chrono::year{date.year()},
                      std::chrono::month{static_cast<unsigned>(date.month())},
                      std::chrono::day{static_cast<unsigned>(date.day())}};
}

QString toIsoTime(core::TimeOfDay time) {
    const auto minutes = static_cast<int>(time.count());
    return QString::fromStdString(
        std::format("{:02}:{:02}", minutes / kMinutesPerHour, minutes % kMinutesPerHour));
}

core::Result<core::TimeOfDay> parseIsoTime(const QString& text) {
    const QTime time = QTime::fromString(text, QStringLiteral("HH:mm"));
    if (text.size() != kIsoTimeLength || !time.isValid()) {
        return std::unexpected(damagedValue("time", text));
    }
    return core::TimeOfDay{time.hour() * kMinutesPerHour + time.minute()};
}

QString toIsoTimestamp(core::Timestamp timestamp) {
    return QDateTime::fromSecsSinceEpoch(timestamp.time_since_epoch().count(), QTimeZone::UTC)
        .toString(Qt::ISODate);
}

core::Result<core::Timestamp> parseIsoTimestamp(const QString& text) {
    const QDateTime dateTime = QDateTime::fromString(text, Qt::ISODate);
    const core::Timestamp timestamp{std::chrono::seconds{dateTime.toSecsSinceEpoch()}};
    // Qt::ISODate also reads other forms, some of them as local time. Only what toIsoTimestamp()
    // writes comes back as the same text.
    if (!dateTime.isValid() || toIsoTimestamp(timestamp) != text) {
        return std::unexpected(damagedValue("timestamp", text));
    }
    return timestamp;
}

QVariant textValue(std::string_view text) {
    if (text.empty()) {
        return QStringLiteral("");
    }
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QVariant nameValue(std::string_view name) { return textValue(core::trimmed(name)); }

QVariant optionalTextValue(const std::optional<std::string>& text) {
    if (!text) {
        return {};
    }
    const std::string value = core::trimmed(*text);
    return value.empty() ? QVariant{} : textValue(value);
}

QVariant optionalValue(const std::optional<int>& value) {
    return value ? QVariant(*value) : QVariant{};
}

QVariant optionalRealValue(const std::optional<double>& value) {
    return value ? QVariant(*value) : QVariant{};
}

QVariant dateValue(core::Date date) { return toIsoDate(date); }

QVariant optionalDateValue(const std::optional<core::Date>& date) {
    return date ? dateValue(*date) : QVariant{};
}

QVariant optionalTimeValue(const std::optional<core::TimeOfDay>& time) {
    return time ? QVariant(toIsoTime(*time)) : QVariant{};
}

QVariant optionalTimestampValue(const std::optional<core::Timestamp>& timestamp) {
    return timestamp ? QVariant(toIsoTimestamp(*timestamp)) : QVariant{};
}

QVariant bytesValue(std::span<const std::uint8_t> bytes) {
    return QByteArray(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<qsizetype>(bytes.size()));
}

QVariant RowReader::next() { return m_query.value(m_column++); }

void RowReader::keepFirstError(const core::Error& error) {
    if (m_status) {
        m_status = std::unexpected(core::Error{core::ErrorCode::Database, error.detail, {}});
    }
}

std::string RowReader::text() { return next().toString().toStdString(); }

std::optional<std::string> RowReader::optionalText() {
    const QVariant value = next();
    return value.isNull() ? std::nullopt : std::optional(value.toString().toStdString());
}

int RowReader::checkedInt(const QVariant& value) {
    bool isNumber = false;
    const qlonglong number = value.toLongLong(&isNumber);
    if (isNumber && number >= std::numeric_limits<int>::min() &&
        number <= std::numeric_limits<int>::max()) {
        return static_cast<int>(number);
    }
    keepFirstError(core::Error{core::ErrorCode::Database,
                               std::format("column '{}': stored value '{}' does not fit an int",
                                           m_query.record().fieldName(m_column - 1).toStdString(),
                                           value.toString().toStdString()),
                               {}});
    return 0;
}

int RowReader::integer() { return checkedInt(next()); }

std::optional<int> RowReader::optionalInteger() {
    const QVariant value = next();
    return value.isNull() ? std::nullopt : std::optional(checkedInt(value));
}

bool RowReader::boolean() { return integer() != 0; }

double RowReader::checkedReal(const QVariant& value) {
    bool isNumber = false;
    const double number = value.toDouble(&isNumber);
    if (isNumber) {
        return number;
    }
    keepFirstError(core::Error{core::ErrorCode::Database,
                               std::format("column '{}': stored value '{}' is not a number",
                                           m_query.record().fieldName(m_column - 1).toStdString(),
                                           value.toString().toStdString()),
                               {}});
    return 0.0;
}

double RowReader::real() { return checkedReal(next()); }

std::optional<double> RowReader::optionalReal() {
    const QVariant value = next();
    return value.isNull() ? std::nullopt : std::optional(checkedReal(value));
}

std::vector<std::uint8_t> RowReader::bytes() {
    const QByteArray value = next().toByteArray();
    const auto* begin = reinterpret_cast<const std::uint8_t*>(value.constData());
    return std::vector<std::uint8_t>(begin, begin + value.size());
}

core::Date RowReader::date() { return unwrap(parseIsoDate(next().toString())); }

std::optional<core::Date> RowReader::optionalDate() {
    const QVariant value = next();
    return value.isNull() ? std::nullopt
                          : std::optional<core::Date>(unwrap(parseIsoDate(value.toString())));
}

std::optional<core::TimeOfDay> RowReader::optionalTime() {
    const QVariant value = next();
    return value.isNull() ? std::nullopt
                          : std::optional<core::TimeOfDay>(unwrap(parseIsoTime(value.toString())));
}

std::optional<core::Timestamp> RowReader::optionalTimestamp() {
    const QVariant value = next();
    return value.isNull()
               ? std::nullopt
               : std::optional<core::Timestamp>(unwrap(parseIsoTimestamp(value.toString())));
}

std::vector<core::Position> RowReader::positions() {
    return unwrap(core::positionsFromDbText(text()));
}

} // namespace fdss::data
