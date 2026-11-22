#pragma once

#include "core/util/Error.hpp"

#include <QString>

#include <span>
#include <vector>

class QSqlDatabase;

// Migration files are src/data/migrations/NNNN_name.sql, embedded as Qt resources and applied in
// order, starting at 0001 without gaps.
//
// Statement splitting: one statement per ';' at the end of a line. Migration files must not contain
// ';' inside literals or triggers. Lines starting with "--" are dropped. A second statement on the
// same line, or a comment after the ';', is not split; it fails with a Database error instead of
// running only partly.

namespace fdss::data {

class DatabaseManager;

struct Migration {
    int version = 0;
    QString name;
    QString sql;
};

// Reads the embedded migrations, sorted by version. A bad file name, a gap, a duplicate or no files
// at all is Internal: the build is broken, not the user's data.
[[nodiscard]] core::Result<std::vector<Migration>> embeddedMigrations();

// Splits a migration file into statements (see the rule above). Accepts CRLF line endings.
[[nodiscard]] std::vector<QString> splitSqlStatements(const QString& sql);

// 0 for an empty database. A database with tables but without a readable schema_version is not
// ours: NotADatabase. Only reads, so it is safe on a file the app must not change.
[[nodiscard]] core::Result<int> readSchemaVersion(const QSqlDatabase& db);

// Applies every migration newer than the current version, each in its own Transaction together with
// the schema_version update. Foreign keys are off while a migration runs, so it may rebuild a
// table; PRAGMA foreign_key_check before the commit fails it when a row lost its parent. A failing
// statement is a Database error with the migration number in detail; the database stays at the last
// version that succeeded, with foreign keys on again.
[[nodiscard]] core::Status applyMigrations(DatabaseManager& db,
                                           std::span<const Migration> migrations);

} // namespace fdss::data
