#include "data/db/DatabaseManager.hpp"

#include "data/db/AppPaths.hpp"
#include "data/db/Backup.hpp"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>

#include <format>
#include <utility>

namespace fdss::data {

namespace {

constexpr auto kDriverName = "QSQLITE";
constexpr auto kPreMigrationReason = "pre_migration";

// SQLite primary result codes (sqlite3.h is not part of the Qt API).
constexpr int kSqliteBusy = 5;
constexpr int kSqliteLocked = 6;
constexpr int kSqliteNoMemory = 7;
constexpr int kSqliteReadOnly = 8;
constexpr int kSqliteInterrupt = 9;
constexpr int kSqliteIoError = 10;
constexpr int kSqliteCorrupt = 11;
constexpr int kSqliteFull = 13;
constexpr int kSqliteCantOpen = 14;
constexpr int kSqliteNotADatabase = 26;

// 0 when the error carries no SQLite code. Extended result codes keep the primary code in the low
// byte.
int primaryResultCode(const QSqlError& error) {
    bool isNumber = false;
    const int nativeCode = error.nativeErrorCode().toInt(&isNumber);
    return isNumber ? nativeCode & 0xFF : 0;
}

core::ErrorCode errorCodeFor(const QSqlError& error) {
    switch (primaryResultCode(error)) {
    case kSqliteNotADatabase:
    case kSqliteCorrupt:
        return core::ErrorCode::NotADatabase;
    case kSqliteBusy:
    case kSqliteLocked:
        return core::ErrorCode::DatabaseLocked;
    case kSqliteCantOpen:
    case kSqliteReadOnly:
    case kSqliteIoError:
    case kSqliteFull:
        return core::ErrorCode::Io;
    default:
        return core::ErrorCode::Database;
    }
}

// The errors after which SQLite may have rolled back the whole transaction by itself.
bool mayLoseTheTransaction(const QSqlError& error) {
    switch (primaryResultCode(error)) {
    case kSqliteFull:
    case kSqliteIoError:
    case kSqliteBusy:
    case kSqliteLocked:
    case kSqliteNoMemory:
    case kSqliteInterrupt:
        return true;
    default:
        return false;
    }
}

// For opening a connection, where no Transaction can be open yet.
core::Status execStatement(const QSqlDatabase& db, const QString& sql, std::string_view context) {
    QSqlQuery query(db);
    if (!query.prepare(sql) || !query.exec()) {
        return std::unexpected(describeSqlError(query.lastError(), context));
    }
    return {};
}

// Sets what every connection needs: busy timeout and enforced foreign keys.
core::Status connect(QSqlDatabase& db, const QString& databaseName, int busyTimeoutMs) {
    db.setDatabaseName(databaseName);
    // A connect option, not a PRAGMA: PRAGMA values cannot be bound.
    db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=%1").arg(busyTimeoutMs));
    if (!db.open()) {
        return std::unexpected(describeSqlError(db.lastError(), "open database"));
    }
    if (auto status =
            execStatement(db, QStringLiteral("PRAGMA foreign_keys = ON"), "enable foreign keys");
        !status) {
        return status;
    }
    QSqlQuery query(db);
    if (!query.prepare(QStringLiteral("PRAGMA foreign_keys")) || !query.exec() || !query.next()) {
        return std::unexpected(describeSqlError(query.lastError(), "read foreign_keys"));
    }
    if (query.value(0).toInt() != 1) {
        return std::unexpected(
            core::Error{core::ErrorCode::Database, "foreign_keys did not stay enabled", {}});
    }
    return {};
}

core::Status enableWal(const QSqlDatabase& db) {
    QSqlQuery query(db);
    if (!query.prepare(QStringLiteral("PRAGMA journal_mode = WAL")) || !query.exec() ||
        !query.next()) {
        return std::unexpected(describeSqlError(query.lastError(), "enable WAL"));
    }
    // SQLite keeps the old mode where WAL is impossible (some network drives). The app still works.
    const QString mode = query.value(0).toString();
    if (mode.compare(QLatin1StringView("wal"), Qt::CaseInsensitive) != 0) {
        qWarning("DatabaseManager: journal mode is '%s', expected 'wal'", qUtf8Printable(mode));
    }
    return {};
}

// In WAL mode reads are not blocked by a writer, so only taking the write lock shows a database
// held by another process.
core::Status checkWriteLock(const QSqlDatabase& db) {
    if (auto status = execStatement(db, QStringLiteral("BEGIN IMMEDIATE"), "take the write lock");
        !status) {
        return status;
    }
    return execStatement(db, QStringLiteral("ROLLBACK"), "release the write lock");
}

} // namespace

core::Error describeSqlError(const QSqlError& error, std::string_view context) {
    return core::Error{errorCodeFor(error),
                       std::format("{}: {} (SQLite code {})", context, error.text().toStdString(),
                                   error.nativeErrorCode().toStdString()),
                       {}};
}

core::Error DatabaseManager::sqlError(const QSqlError& error, std::string_view context) {
    if (m_transactionDepth > 0 && mayLoseTheTransaction(error)) {
        m_transactionBroken = true;
    }
    return describeSqlError(error, context);
}

bool DatabaseManager::isTransactionBroken() const { return m_transactionBroken; }

core::Status DatabaseManager::execSql(const QString& sql, std::string_view context) {
    // After a lost transaction this statement would run on its own and commit at once.
    if (m_transactionBroken) {
        return std::unexpected(brokenTransactionError());
    }
    return runStatement(sql, context);
}

core::Status DatabaseManager::runStatement(const QString& sql, std::string_view context) {
    QSqlQuery query(database());
    if (!query.prepare(sql) || !query.exec()) {
        return std::unexpected(sqlError(query.lastError(), context));
    }
    return {};
}

core::Error DatabaseManager::brokenTransactionError() {
    return core::Error{core::ErrorCode::Database,
                       "an earlier error may have rolled back the transaction; destroy the "
                       "outermost Transaction before going on",
                       {}};
}

core::Result<std::unique_ptr<DatabaseManager>> DatabaseManager::open(const QString& filePath,
                                                                     OpenOptions options) {
    if (filePath.isEmpty()) {
        return std::unexpected(
            core::Error{core::ErrorCode::InvalidInput, "empty database path", "filePath"});
    }
    return openConnection(filePath, std::move(options));
}

core::Result<std::unique_ptr<DatabaseManager>> DatabaseManager::openInMemory(OpenOptions options) {
    return openConnection(QString{}, std::move(options));
}

core::Result<std::unique_ptr<DatabaseManager>>
DatabaseManager::openConnection(const QString& filePath, OpenOptions options) {
    if (!options.migrations) {
        auto embedded = embeddedMigrations();
        if (!embedded) {
            return std::unexpected(embedded.error());
        }
        options.migrations = std::move(*embedded);
    }
    const std::vector<Migration>& migrations = *options.migrations;
    if (migrations.empty()) {
        return std::unexpected(
            core::Error{core::ErrorCode::Internal, "no migrations available", {}});
    }
    if (!QSqlDatabase::isDriverAvailable(QString::fromLatin1(kDriverName))) {
        return std::unexpected(
            core::Error{core::ErrorCode::Internal, "the QSQLITE driver plugin is missing", {}});
    }

    // Declared before `db`: the last copy of the connection must be gone when the manager removes
    // it.
    auto manager = std::make_unique<DatabaseManager>(PassKey{}, filePath, options.backupDirectory,
                                                     options.busyTimeoutMs);
    QSqlDatabase db = manager->database();
    const bool isFile = !filePath.isEmpty();
    if (auto status =
            connect(db, isFile ? filePath : QStringLiteral(":memory:"), options.busyTimeoutMs);
        !status) {
        return std::unexpected(status.error());
    }

    // Everything up to here only reads. WAL writes to the file header, so it comes after the checks
    // that refuse a file the app must not touch.
    const core::Result<int> current = readSchemaVersion(db);
    if (!current) {
        return std::unexpected(current.error());
    }
    const int latest = migrations.back().version;
    if (*current > latest) {
        return std::unexpected(core::Error{
            core::ErrorCode::IncompatibleVersion,
            std::format("database is at version {}, this app knows up to {}", *current, latest),
            {}});
    }
    manager->m_schemaVersion = *current;

    if (isFile) {
        if (auto status = enableWal(db); !status) {
            return std::unexpected(status.error());
        }
        if (auto status = checkWriteLock(db); !status) {
            return std::unexpected(status.error());
        }
    }
    // A brand-new file (version 0) has nothing to protect.
    if (isFile && *current > 0 && *current < latest) {
        if (auto backedUp = manager->backup(kPreMigrationReason); !backedUp) {
            return std::unexpected(backedUp.error());
        }
    }
    if (auto status = applyMigrations(*manager, migrations); !status) {
        return std::unexpected(status.error());
    }
    return manager;
}

DatabaseManager::DatabaseManager(PassKey, QString filePath, QString backupDirectory,
                                 int busyTimeoutMs)
    : m_connectionName(QStringLiteral("fdss_%1").arg(reinterpret_cast<quintptr>(this), 0, 16)),
      m_filePath(std::move(filePath)), m_backupDirectory(std::move(backupDirectory)),
      m_busyTimeoutMs(busyTimeoutMs) {
    QSqlDatabase::addDatabase(QString::fromLatin1(kDriverName), m_connectionName);
}

DatabaseManager::~DatabaseManager() {
    Q_ASSERT(m_transactionDepth == 0);
    {
        QSqlDatabase db = database();
        db.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

QSqlDatabase DatabaseManager::database() const {
    return QSqlDatabase::database(m_connectionName, false);
}

int DatabaseManager::schemaVersion() const { return m_schemaVersion; }

QString DatabaseManager::filePath() const { return m_filePath; }

core::Result<QSqlDatabase> DatabaseManager::openWorkerConnection(const QString& name) const {
    if (m_filePath.isEmpty()) {
        return std::unexpected(core::Error{
            core::ErrorCode::Internal, "an in-memory database has no worker connections", {}});
    }
    if (name.isEmpty() || QSqlDatabase::contains(name)) {
        return std::unexpected(
            core::Error{core::ErrorCode::Conflict,
                        std::format("connection name '{}' is empty or in use", name.toStdString()),
                        {}});
    }
    core::Status status;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QString::fromLatin1(kDriverName), name);
        status = connect(db, m_filePath, m_busyTimeoutMs);
        if (status) {
            return db;
        }
    }
    QSqlDatabase::removeDatabase(name);
    return std::unexpected(status.error());
}

core::Result<QString> DatabaseManager::backup(std::string_view reason) {
    if (m_filePath.isEmpty()) {
        return std::unexpected(
            core::Error{core::ErrorCode::Internal, "cannot back up an in-memory database", {}});
    }
    if (m_transactionDepth > 0) {
        return std::unexpected(
            core::Error{core::ErrorCode::Internal, "cannot back up inside a transaction", {}});
    }
    QString directory = m_backupDirectory;
    if (directory.isEmpty()) {
        auto defaultDirectory = AppPaths::backupDirectory();
        if (!defaultDirectory) {
            return std::unexpected(defaultDirectory.error());
        }
        directory = std::move(*defaultDirectory);
    }

    auto path = createBackup(database(), directory, reason, QDateTime::currentDateTime());
    if (path) {
        // The new backup exists, so a leftover old file is worth a log line, not a failure.
        if (const auto pruned = pruneBackups(directory, *path); !pruned) {
            qWarning("DatabaseManager: %s", pruned.error().detail.c_str());
        }
    }
    return path;
}

} // namespace fdss::data
