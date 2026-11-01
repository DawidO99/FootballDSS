#pragma once

#include "core/domain/DateTypes.hpp"
#include "core/domain/Enums.hpp"
#include "core/util/Error.hpp"
#include "data/db/DatabaseManager.hpp"

#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QVariant>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// What every repository shares. Every statement goes through runSql(), so its failure reaches
// DatabaseManager::sqlError(). A repository returns at the first error (AGENTS section 7, P06
// Decisions): after a lost transaction a further statement would commit on its own.

namespace fdss::data {

// What a FOREIGN KEY failure means. The SQLite message does not say, the statement does.
enum class ForeignKeyFailure { MissingParent, RowInUse };

// sqlError() plus the mapping of constraint violations: UNIQUE gives Conflict with the column in
// `field`; FOREIGN KEY gives Conflict for RowInUse and InvalidInput for MissingParent; NOT NULL,
// CHECK and type errors give InvalidInput (NOT NULL also with `field`).
[[nodiscard]] core::Error
statementError(DatabaseManager& db, const QSqlError& error, std::string_view context,
               ForeignKeyFailure foreignKeyFailure = ForeignKeyFailure::MissingParent);

// Prepares, binds positional values and runs one statement. Keep the returned query only as long as
// its rows are needed: it must be gone before the Transaction around it ends.
[[nodiscard]] core::Result<QSqlQuery>
runSql(DatabaseManager& db, const QString& sql, const QVariantList& values,
       std::string_view context,
       ForeignKeyFailure foreignKeyFailure = ForeignKeyFailure::MissingParent);

// Runs an INSERT and returns the id of the new row.
[[nodiscard]] core::Result<std::int64_t> insertRow(DatabaseManager& db, const QString& sql,
                                                   const QVariantList& values,
                                                   std::string_view context);

// Runs an UPDATE or DELETE of one row. No row matched: NotFound.
[[nodiscard]] core::Status
changeRow(DatabaseManager& db, const QString& sql, const QVariantList& values,
          std::string_view context,
          ForeignKeyFailure foreignKeyFailure = ForeignKeyFailure::MissingParent);

// NotFound unless the statement returns a row.
[[nodiscard]] core::Status requireRow(DatabaseManager& db, const QString& sql,
                                      const QVariantList& values, std::string_view context);

// --- ISO text. Dates YYYY-MM-DD, times HH:MM, timestamps UTC yyyy-MM-ddTHH:mm:ssZ. ---
//
// Formatting keeps an impossible value visible (2026-02-30), so the CHECK of the column rejects it;
// it never turns into NULL, which a nullable column would accept. Stored text that does not parse
// means a damaged database: Database.

[[nodiscard]] QString toIsoDate(core::Date date);
[[nodiscard]] core::Result<core::Date> parseIsoDate(const QString& text);
[[nodiscard]] QString toIsoTime(core::TimeOfDay time);
[[nodiscard]] core::Result<core::TimeOfDay> parseIsoTime(const QString& text);
[[nodiscard]] QString toIsoTimestamp(core::Timestamp timestamp);
[[nodiscard]] core::Result<core::Timestamp> parseIsoTimestamp(const QString& text);

// --- Values to bind. An invalid QVariant binds NULL. ---

// std::int64_t is `long` on Linux, which is ambiguous for the QVariant constructors.
template <class Id> [[nodiscard]] QVariant idValue(Id id) {
    return QVariant(static_cast<qlonglong>(id.value));
}
template <class Id> [[nodiscard]] QVariant optionalIdValue(const std::optional<Id>& id) {
    return id ? idValue(*id) : QVariant{};
}
// Never NULL: the driver binds a null QString as NULL, and an empty text is a value here.
[[nodiscard]] QVariant textValue(std::string_view text);
// Trimmed, so what is stored has the length the core validation saw.
[[nodiscard]] QVariant nameValue(std::string_view name);
// Trimmed; empty after trimming is NULL.
[[nodiscard]] QVariant optionalTextValue(const std::optional<std::string>& text);
template <class E> [[nodiscard]] QVariant enumValue(E value) { return textValue(toDbText(value)); }
template <class E> [[nodiscard]] QVariant optionalEnumValue(const std::optional<E>& value) {
    return value ? enumValue(*value) : QVariant{};
}
[[nodiscard]] QVariant optionalValue(const std::optional<int>& value);
[[nodiscard]] QVariant dateValue(core::Date date);
[[nodiscard]] QVariant optionalDateValue(const std::optional<core::Date>& date);
[[nodiscard]] QVariant optionalTimeValue(const std::optional<core::TimeOfDay>& time);
[[nodiscard]] QVariant optionalTimestampValue(const std::optional<core::Timestamp>& timestamp);
[[nodiscard]] QVariant bytesValue(std::span<const std::uint8_t> bytes);

// Reads the columns of the current row from left to right. The first unreadable value is kept as a
// Database error and later reads go on with defaults, so a row function stays a flat list of
// fields.
class RowReader {
public:
    explicit RowReader(const QSqlQuery& query) : m_query(query) {}

    [[nodiscard]] std::string text();
    [[nodiscard]] std::optional<std::string> optionalText();
    [[nodiscard]] int integer();
    [[nodiscard]] std::optional<int> optionalInteger();
    [[nodiscard]] bool boolean();
    [[nodiscard]] std::vector<std::uint8_t> bytes();
    [[nodiscard]] core::Date date();
    [[nodiscard]] std::optional<core::Date> optionalDate();
    [[nodiscard]] std::optional<core::TimeOfDay> optionalTime();
    [[nodiscard]] std::optional<core::Timestamp> optionalTimestamp();
    [[nodiscard]] std::vector<core::Position> positions();

    template <class Id> [[nodiscard]] Id id() { return Id{next().toLongLong()}; }
    template <class Id> [[nodiscard]] std::optional<Id> optionalId() {
        const QVariant value = next();
        return value.isNull() ? std::nullopt : std::optional<Id>(Id{value.toLongLong()});
    }
    template <class E> [[nodiscard]] E enumeration() { return unwrap(core::fromDbText<E>(text())); }
    template <class E> [[nodiscard]] std::optional<E> optionalEnumeration() {
        const std::optional<std::string> value = optionalText();
        return value ? std::optional<E>(unwrap(core::fromDbText<E>(*value))) : std::nullopt;
    }

    // The first error of this row, if any.
    [[nodiscard]] const core::Status& status() const noexcept { return m_status; }

private:
    [[nodiscard]] QVariant next();
    void keepFirstError(const core::Error& error);

    template <class T> [[nodiscard]] T unwrap(core::Result<T> value) {
        if (value) {
            return std::move(*value);
        }
        keepFirstError(value.error());
        return T{};
    }

    const QSqlQuery& m_query;
    int m_column = 0;
    core::Status m_status;
};

// Every row of a SELECT, read by `readRow(RowReader&)`.
template <class ReadRow, class T = std::invoke_result_t<ReadRow&, RowReader&>>
[[nodiscard]] core::Result<std::vector<T>> selectAll(DatabaseManager& db, const QString& sql,
                                                     const QVariantList& values,
                                                     std::string_view context, ReadRow readRow) {
    auto query = runSql(db, sql, values, context);
    if (!query) {
        return std::unexpected(query.error());
    }
    std::vector<T> rows;
    while (query->next()) {
        RowReader reader(*query);
        T row = readRow(reader);
        if (!reader.status()) {
            return std::unexpected(reader.status().error());
        }
        rows.push_back(std::move(row));
    }
    // next() also returns false when stepping fails.
    if (query->lastError().isValid()) {
        return std::unexpected(statementError(db, query->lastError(), context));
    }
    return rows;
}

// The NotFound error of a statement that matched no row.
[[nodiscard]] core::Error rowNotFound(std::string_view context);

// The first row, read by `readRow(RowReader&)`. No row: NotFound.
template <class ReadRow, class T = std::invoke_result_t<ReadRow&, RowReader&>>
[[nodiscard]] core::Result<T> selectOne(DatabaseManager& db, const QString& sql,
                                        const QVariantList& values, std::string_view context,
                                        ReadRow readRow) {
    auto query = runSql(db, sql, values, context);
    if (!query) {
        return std::unexpected(query.error());
    }
    if (!query->next()) {
        if (query->lastError().isValid()) {
            return std::unexpected(statementError(db, query->lastError(), context));
        }
        return std::unexpected(rowNotFound(context));
    }
    RowReader reader(*query);
    T row = readRow(reader);
    if (!reader.status()) {
        return std::unexpected(reader.status().error());
    }
    return row;
}

} // namespace fdss::data
