#include "data/db/Migrations.hpp"

#include "data/db/DatabaseManager.hpp"
#include "data/db/Transaction.hpp"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringTokenizer>

#include <algorithm>
#include <format>
#include <ranges>
#include <utility>

namespace fdss::data {

namespace {

constexpr auto kResourceDirectory = ":/fdss/migrations";

core::Error internalError(std::string detail) {
    return core::Error{core::ErrorCode::Internal, std::move(detail), {}};
}

// Versions must be exactly 1, 2, 3, ...
core::Status checkSequence(std::span<const Migration> migrations) {
    if (migrations.empty()) {
        return std::unexpected(internalError("no migrations available"));
    }
    int expected = 1;
    for (const Migration& migration : migrations) {
        if (migration.version != expected) {
            return std::unexpected(internalError(std::format(
                "migration version {} found where {} was expected", migration.version, expected)));
        }
        ++expected;
    }
    return {};
}

core::Error migrationError(const Migration& migration, core::ErrorCode code,
                           const std::string& reason) {
    return core::Error{code,
                       std::format("migration {} ({}): {}", migration.version,
                                   migration.name.toStdString(), reason),
                       {}};
}

// SQLite ignores PRAGMA foreign_keys inside a transaction, so this runs between them. Reads the
// value back: a pragma that did nothing must not pass unnoticed.
core::Status setForeignKeys(DatabaseManager& db, bool enabled) {
    if (auto status = db.execSql(enabled ? QStringLiteral("PRAGMA foreign_keys = ON")
                                         : QStringLiteral("PRAGMA foreign_keys = OFF"),
                                 "switch foreign keys");
        !status) {
        return status;
    }
    QSqlQuery query(db.database());
    if (!query.prepare(QStringLiteral("PRAGMA foreign_keys")) || !query.exec() || !query.next()) {
        return std::unexpected(db.sqlError(query.lastError(), "read foreign_keys"));
    }
    if ((query.value(0).toInt() == 1) != enabled) {
        return std::unexpected(
            core::Error{core::ErrorCode::Database,
                        std::format("foreign_keys did not switch {}", enabled ? "on" : "off"),
                        {}});
    }
    return {};
}

// Inside the transaction of a migration that ran without foreign keys: any row that a migration
// left without its parent fails the migration.
core::Status checkForeignKeys(DatabaseManager& db, const Migration& migration) {
    QSqlQuery query(db.database());
    if (!query.prepare(QStringLiteral("PRAGMA foreign_key_check")) || !query.exec()) {
        return std::unexpected(
            migrationError(migration, core::ErrorCode::Database,
                           db.sqlError(query.lastError(), "foreign_key_check").detail));
    }
    if (query.next()) {
        return std::unexpected(migrationError(
            migration, core::ErrorCode::Database,
            std::format("row {} of {} has no parent in {}", query.value(1).toLongLong(),
                        query.value(0).toString().toStdString(),
                        query.value(2).toString().toStdString())));
    }
    return {};
}

core::Status applyInTransaction(DatabaseManager& db, const Migration& migration) {
    auto transaction = Transaction::begin(db);
    if (!transaction) {
        return std::unexpected(
            migrationError(migration, transaction.error().code, transaction.error().detail));
    }

    const QSqlDatabase connection = db.database();
    for (const QString& statement : splitSqlStatements(migration.sql)) {
        QSqlQuery query(connection);
        if (!query.prepare(statement) || !query.exec()) {
            return std::unexpected(
                migrationError(migration, core::ErrorCode::Database,
                               db.sqlError(query.lastError(), statement.toStdString()).detail));
        }
    }

    if (auto status = checkForeignKeys(db, migration); !status) {
        return status;
    }

    QSqlQuery update(connection);
    if (!update.prepare(
            QStringLiteral("INSERT INTO schema_version (id, version) VALUES (1, ?) "
                           "ON CONFLICT (id) DO UPDATE SET version = excluded.version"))) {
        return std::unexpected(migrationError(
            migration, core::ErrorCode::Database,
            db.sqlError(update.lastError(), "prepare schema_version update").detail));
    }
    update.bindValue(0, migration.version);
    if (!update.exec()) {
        return std::unexpected(
            migrationError(migration, core::ErrorCode::Database,
                           db.sqlError(update.lastError(), "update schema_version").detail));
    }

    if (auto committed = transaction->commit(); !committed) {
        return std::unexpected(
            migrationError(migration, committed.error().code, committed.error().detail));
    }
    return {};
}

// The procedure of https://www.sqlite.org/lang_altertable.html for changes ALTER TABLE cannot make
// (P19 decision 3): foreign keys off around the transaction, so that a table its children point at
// with ON DELETE RESTRICT can be rebuilt; foreign_key_check before the commit; foreign keys on
// again also after a failure.
core::Status applyMigration(DatabaseManager& db, const Migration& migration) {
    if (auto status = setForeignKeys(db, false); !status) {
        return std::unexpected(
            migrationError(migration, status.error().code, status.error().detail));
    }
    const core::Status applied = applyInTransaction(db, migration);
    if (auto status = setForeignKeys(db, true); !status) {
        // The migration's own error says more; a connection without foreign keys must not go on.
        return applied ? std::unexpected(
                             migrationError(migration, status.error().code, status.error().detail))
                       : applied;
    }
    return applied;
}

} // namespace

core::Result<std::vector<Migration>> embeddedMigrations() {
    static const QRegularExpression kFileName(QStringLiteral(R"(^(\d{4})_([a-z0-9_]+)\.sql$)"));

    std::vector<Migration> migrations;
    const QDir directory(QString::fromLatin1(kResourceDirectory));
    const QFileInfoList files = directory.entryInfoList(QDir::Files);
    for (const QFileInfo& info : files) {
        const QRegularExpressionMatch match = kFileName.match(info.fileName());
        if (!match.hasMatch()) {
            return std::unexpected(internalError(
                std::format("bad migration file name: {}", info.fileName().toStdString())));
        }
        QFile file(info.filePath());
        if (!file.open(QIODevice::ReadOnly)) {
            return std::unexpected(internalError(
                std::format("cannot read migration: {}", info.filePath().toStdString())));
        }
        migrations.push_back(
            {match.captured(1).toInt(), match.captured(2), QString::fromUtf8(file.readAll())});
    }

    std::ranges::sort(migrations, {}, &Migration::version);
    if (auto status = checkSequence(migrations); !status) {
        return std::unexpected(status.error());
    }
    return migrations;
}

std::vector<QString> splitSqlStatements(const QString& sql) {
    std::vector<QString> statements;
    QString current;
    for (const QStringView line : qTokenize(sql, u'\n')) {
        // Also removes the '\r' of a CRLF checkout on Windows.
        const QStringView trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(u"--")) {
            continue;
        }
        current += trimmed;
        current += u'\n';
        if (trimmed.endsWith(u';')) {
            statements.push_back(current.trimmed());
            current.clear();
        }
    }
    if (!current.isEmpty()) {
        statements.push_back(current.trimmed());
    }
    return statements;
}

core::Result<int> readSchemaVersion(const QSqlDatabase& db) {
    QSqlQuery tables(db);
    if (!tables.prepare(QStringLiteral(
            "SELECT count(*), coalesce(sum(type = 'table' AND name = 'schema_version'), 0) "
            "FROM sqlite_master")) ||
        !tables.exec() || !tables.next()) {
        return std::unexpected(describeSqlError(tables.lastError(), "read sqlite_master"));
    }
    if (tables.value(0).toInt() == 0) {
        return 0;
    }
    if (tables.value(1).toInt() == 0) {
        return std::unexpected(core::Error{
            core::ErrorCode::NotADatabase, "database has tables but no schema_version", {}});
    }

    QSqlQuery version(db);
    if (!version.prepare(QStringLiteral("SELECT version FROM schema_version")) || !version.exec()) {
        core::Error error = describeSqlError(version.lastError(), "read schema_version");
        // A plain SQL error means another program's table with the same name. A lock stays a lock.
        if (error.code == core::ErrorCode::Database) {
            error.code = core::ErrorCode::NotADatabase;
        }
        return std::unexpected(std::move(error));
    }
    bool isNumber = false;
    const int value = version.next() ? version.value(0).toInt(&isNumber) : 0;
    if (!isNumber) {
        return std::unexpected(core::Error{
            core::ErrorCode::NotADatabase, "schema_version has no valid version row", {}});
    }
    return value;
}

core::Status applyMigrations(DatabaseManager& db, std::span<const Migration> migrations) {
    if (auto status = checkSequence(migrations); !status) {
        return status;
    }
    const core::Result<int> current = readSchemaVersion(db.database());
    if (!current) {
        return std::unexpected(current.error());
    }
    db.m_schemaVersion = *current;

    const auto isPending = [version = *current](const Migration& migration) {
        return migration.version > version;
    };
    for (const Migration& migration : migrations | std::views::filter(isPending)) {
        if (auto status = applyMigration(db, migration); !status) {
            return status;
        }
        db.m_schemaVersion = migration.version;
    }
    return {};
}

} // namespace fdss::data
