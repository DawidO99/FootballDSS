#pragma once

#include "core/util/Error.hpp"
#include "data/db/Migrations.hpp"

#include <QSqlDatabase>
#include <QString>

#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

class QSqlError;

namespace fdss::data {

inline constexpr int kDefaultBusyTimeoutMs = 5000;

// At namespace scope: a nested struct with default member initializers cannot be a default argument
// of a function of its enclosing class on GCC and Clang.
struct OpenOptions {
    int busyTimeoutMs = kDefaultBusyTimeoutMs;
    // Empty: AppPaths::backupDirectory().
    QString backupDirectory;
    // nullopt: the embedded migrations. Set by tests to exercise upgrades and failures.
    std::optional<std::vector<Migration>> migrations;
};

// Turns a failed QtSql call into an Error. The SQLite result code picks the ErrorCode; `context`
// (what was being done) goes into detail.
[[nodiscard]] core::Error sqlError(const QSqlError& error, std::string_view context);

// Prepares and runs one statement without parameters.
[[nodiscard]] core::Status execSql(const QSqlDatabase& db, const QString& sql,
                                   std::string_view context);

// Owns the connection of the thread that opened the database. Repositories ask for it on each call
// and never store a copy.
class DatabaseManager {
    // Lets open() use make_unique while keeping the constructor out of the public interface.
    struct PassKey {
        explicit PassKey() = default;
    };

public:
    // Opens or creates the file, applies pragmas, backs up and migrates if needed. A file that is
    // not a database, belongs to something else or comes from a newer app version is not changed.
    [[nodiscard]] static core::Result<std::unique_ptr<DatabaseManager>>
    open(const QString& filePath, OpenOptions options = {});
    // For tests and qml_smoke.
    [[nodiscard]] static core::Result<std::unique_ptr<DatabaseManager>>
    openInMemory(OpenOptions options = {});

    DatabaseManager(PassKey, QString filePath, QString backupDirectory, int busyTimeoutMs);
    ~DatabaseManager(); // closes and removes the named connection
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    [[nodiscard]] QSqlDatabase database() const; // the connection of the creating thread
    [[nodiscard]] int schemaVersion() const;
    [[nodiscard]] QString filePath() const; // empty for in-memory

    // New named connection to the same file for reading on a worker thread; database writes stay on
    // the main-thread connection (AGENTS section 5). Call it on the worker thread. The caller
    // closes it with QSqlDatabase::removeDatabase() after dropping every copy. In-memory: Internal.
    // Name already in use: Conflict.
    [[nodiscard]] core::Result<QSqlDatabase> openWorkerConnection(const QString& name) const;

    // Backs up to the backup folder and removes old backups; returns the backup path. In-memory or
    // inside an open Transaction: Internal. See createBackup() for the reason format.
    core::Result<QString> backup(std::string_view reason);

private:
    friend class Transaction;
    friend core::Status applyMigrations(DatabaseManager& db, std::span<const Migration> migrations);

    [[nodiscard]] static core::Result<std::unique_ptr<DatabaseManager>>
    openConnection(const QString& filePath, OpenOptions options);

    QString m_connectionName;
    QString m_filePath;
    QString m_backupDirectory;
    int m_busyTimeoutMs = kDefaultBusyTimeoutMs;
    int m_schemaVersion = 0;
    int m_transactionDepth = 0;
};

} // namespace fdss::data
