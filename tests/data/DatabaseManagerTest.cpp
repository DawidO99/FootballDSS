#include "data/db/DatabaseManager.hpp"

#include "TestDatabase.hpp"
#include "data/db/Transaction.hpp"

#include <QDir>
#include <QFileInfo>

#include <gtest/gtest.h>

#include <thread>
#include <vector>

using namespace fdss::data;
using namespace fdss::tests;
using fdss::core::ErrorCode;

namespace {

class DatabaseManagerTest : public TempDatabaseTest {};

std::vector<QString> g_qtMessages;

void collectProblems(QtMsgType type, const QMessageLogContext&, const QString& message) {
    if (type != QtDebugMsg && type != QtInfoMsg) {
        g_qtMessages.push_back(message);
    }
}

} // namespace

TEST_F(DatabaseManagerTest, FreshFileIsCreatedAtTheLatestVersion) {
    const auto db = DatabaseManager::open(databasePath(), options());
    ASSERT_TRUE(db.has_value()) << db.error().detail;
    EXPECT_EQ((*db)->schemaVersion(), 4);
    EXPECT_EQ((*db)->filePath(), databasePath());
    EXPECT_TRUE(QFileInfo::exists(databasePath()));
    EXPECT_EQ(
        scalar((*db)->database(), QStringLiteral("SELECT version FROM schema_version")).toInt(), 4);
}

TEST_F(DatabaseManagerTest, FreshFileGetsNoBackup) {
    const auto db = DatabaseManager::open(databasePath(), options());
    ASSERT_TRUE(db.has_value()) << db.error().detail;
    EXPECT_TRUE(filesIn(backupDirectory()).isEmpty());
}

TEST_F(DatabaseManagerTest, ReopenRunsNoMigrationAndMakesNoBackup) {
    {
        const auto first = DatabaseManager::open(databasePath(), options());
        ASSERT_TRUE(first.has_value()) << first.error().detail;
        ASSERT_TRUE(run((*first)->database(),
                        QStringLiteral("INSERT INTO club (name) VALUES ('KS Test')")));
    }
    const auto second = DatabaseManager::open(databasePath(), options());
    ASSERT_TRUE(second.has_value()) << second.error().detail;
    EXPECT_EQ((*second)->schemaVersion(), 4);
    EXPECT_EQ(scalar((*second)->database(), QStringLiteral("SELECT name FROM club")).toString(),
              QStringLiteral("KS Test"));
    EXPECT_TRUE(filesIn(backupDirectory()).isEmpty());
}

TEST_F(DatabaseManagerTest, InMemoryHasTheSchemaAndNoFilePath) {
    const auto db = DatabaseManager::openInMemory();
    ASSERT_TRUE(db.has_value()) << db.error().detail;
    EXPECT_EQ((*db)->schemaVersion(), 4);
    EXPECT_TRUE((*db)->filePath().isEmpty());
    EXPECT_TRUE(tableExists((*db)->database(), QStringLiteral("player")));
}

TEST_F(DatabaseManagerTest, EachInMemoryDatabaseIsSeparate) {
    const auto first = DatabaseManager::openInMemory();
    const auto second = DatabaseManager::openInMemory();
    ASSERT_TRUE(first.has_value()) << first.error().detail;
    ASSERT_TRUE(second.has_value()) << second.error().detail;
    ASSERT_TRUE(
        run((*first)->database(), QStringLiteral("INSERT INTO club (name) VALUES ('KS Test')")));
    EXPECT_EQ(scalar((*second)->database(), QStringLiteral("SELECT count(*) FROM club")).toInt(),
              0);
}

TEST_F(DatabaseManagerTest, FileDatabaseHasAllPragmas) {
    const auto db = DatabaseManager::open(databasePath(), options());
    ASSERT_TRUE(db.has_value()) << db.error().detail;
    const QSqlDatabase connection = (*db)->database();
    EXPECT_EQ(scalar(connection, QStringLiteral("PRAGMA foreign_keys")).toInt(), 1);
    EXPECT_EQ(scalar(connection, QStringLiteral("PRAGMA busy_timeout")).toInt(), 100);
    EXPECT_EQ(scalar(connection, QStringLiteral("PRAGMA journal_mode")).toString(),
              QStringLiteral("wal"));
}

TEST_F(DatabaseManagerTest, InMemoryDatabaseHasForeignKeysAndDefaultTimeout) {
    const auto db = DatabaseManager::openInMemory();
    ASSERT_TRUE(db.has_value()) << db.error().detail;
    const QSqlDatabase connection = (*db)->database();
    EXPECT_EQ(scalar(connection, QStringLiteral("PRAGMA foreign_keys")).toInt(), 1);
    EXPECT_EQ(scalar(connection, QStringLiteral("PRAGMA busy_timeout")).toInt(),
              kDefaultBusyTimeoutMs);
}

TEST_F(DatabaseManagerTest, EmptyPathIsInvalidInput) {
    const auto db = DatabaseManager::open(QString{}, options());
    ASSERT_FALSE(db.has_value());
    EXPECT_EQ(db.error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(db.error().field, "filePath");
}

TEST_F(DatabaseManagerTest, PathWithPolishLettersWorks) {
    const QString directory = m_directory.filePath(QStringLiteral("Łódź Żółć"));
    ASSERT_TRUE(QDir().mkpath(directory));
    const QString path = QDir(directory).filePath(QStringLiteral("klub główny.sqlite"));

    OpenOptions opts = options();
    opts.backupDirectory = QDir(directory).filePath(QStringLiteral("kopie zapasowe"));
    const auto db = DatabaseManager::open(path, opts);
    ASSERT_TRUE(db.has_value()) << db.error().detail;
    EXPECT_TRUE(QFileInfo::exists(path));

    const auto backup = (*db)->backup("manual");
    ASSERT_TRUE(backup.has_value()) << backup.error().detail;
    EXPECT_TRUE(QFileInfo::exists(*backup));
}

// --- Files that must be refused ---

TEST_F(DatabaseManagerTest, RandomBytesAreNotADatabaseAndStayUntouched) {
    QByteArray garbage;
    for (int i = 0; i < 8192; ++i) {
        garbage.append(static_cast<char>((i * 31 + 7) % 251));
    }
    ASSERT_TRUE(writeFile(databasePath(), garbage));

    const auto db = DatabaseManager::open(databasePath(), options());
    ASSERT_FALSE(db.has_value());
    EXPECT_EQ(db.error().code, ErrorCode::NotADatabase) << db.error().detail;
    EXPECT_EQ(readFile(databasePath()), garbage);
}

TEST_F(DatabaseManagerTest, ShortTextFileIsNotADatabaseAndStaysUntouched) {
    const QByteArray text("To jest zwykly plik tekstowy, a nie baza danych.\n");
    ASSERT_TRUE(writeFile(databasePath(), text));

    const auto db = DatabaseManager::open(databasePath(), options());
    ASSERT_FALSE(db.has_value());
    EXPECT_EQ(db.error().code, ErrorCode::NotADatabase) << db.error().detail;
    EXPECT_EQ(readFile(databasePath()), text);
}

TEST_F(DatabaseManagerTest, EmptyFileIsTreatedAsNew) {
    ASSERT_TRUE(writeFile(databasePath(), QByteArray{}));
    const auto db = DatabaseManager::open(databasePath(), options());
    ASSERT_TRUE(db.has_value()) << db.error().detail;
    EXPECT_EQ((*db)->schemaVersion(), 4);
}

TEST_F(DatabaseManagerTest, FileReplacedAfterACleanCloseIsNotADatabase) {
    {
        const auto db = DatabaseManager::open(databasePath(), options());
        ASSERT_TRUE(db.has_value()) << db.error().detail;
    }
    // A clean close leaves no -wal file that could still supply the old pages.
    EXPECT_FALSE(QFileInfo::exists(databasePath() + QStringLiteral("-wal")));
    const QByteArray text("To jest zwykly plik tekstowy.\n");
    ASSERT_TRUE(writeFile(databasePath(), text));

    const auto db = DatabaseManager::open(databasePath(), options());
    ASSERT_FALSE(db.has_value());
    EXPECT_EQ(db.error().code, ErrorCode::NotADatabase) << db.error().detail;
    EXPECT_EQ(readFile(databasePath()), text);
}

TEST_F(DatabaseManagerTest, ForeignTableNamedSchemaVersionIsNotADatabase) {
    {
        const RawConnection other(databasePath());
        ASSERT_TRUE(run(other.database(),
                        QStringLiteral("CREATE TABLE schema_version (installed_rank INTEGER)")));
    }
    const auto db = DatabaseManager::open(databasePath(), options());
    ASSERT_FALSE(db.has_value());
    EXPECT_EQ(db.error().code, ErrorCode::NotADatabase) << db.error().detail;
}

TEST_F(DatabaseManagerTest, OtherSqliteDatabaseIsRefusedAndStaysUntouched) {
    {
        const RawConnection other(databasePath());
        ASSERT_TRUE(
            run(other.database(), QStringLiteral("CREATE TABLE recipes (id INTEGER PRIMARY KEY)")));
    }
    const QByteArray before = readFile(databasePath());

    const auto db = DatabaseManager::open(databasePath(), options());
    ASSERT_FALSE(db.has_value());
    EXPECT_EQ(db.error().code, ErrorCode::NotADatabase) << db.error().detail;
    EXPECT_EQ(readFile(databasePath()), before);
}

TEST_F(DatabaseManagerTest, FutureVersionIsRefusedAndTheFileStaysUntouched) {
    {
        const RawConnection other(databasePath());
        ASSERT_TRUE(run(other.database(),
                        QStringLiteral("CREATE TABLE schema_version (id INTEGER PRIMARY KEY, "
                                       "version INTEGER NOT NULL)")));
        ASSERT_TRUE(
            run(other.database(), QStringLiteral("INSERT INTO schema_version VALUES (1, 999)")));
    }
    const QByteArray before = readFile(databasePath());

    {
        const auto db = DatabaseManager::open(databasePath(), options());
        ASSERT_FALSE(db.has_value());
        EXPECT_EQ(db.error().code, ErrorCode::IncompatibleVersion) << db.error().detail;
    }
    // Enabling WAL would have changed the header.
    EXPECT_EQ(readFile(databasePath()), before);
    EXPECT_TRUE(filesIn(backupDirectory()).isEmpty());
    const RawConnection other(databasePath());
    EXPECT_EQ(scalar(other.database(), QStringLiteral("PRAGMA journal_mode")).toString(),
              QStringLiteral("delete"));
}

TEST_F(DatabaseManagerTest, VersionNewerThanTheKnownMigrationsIsRefused) {
    {
        const auto db =
            DatabaseManager::open(databasePath(), options({firstMigration(), secondMigration()}));
        ASSERT_TRUE(db.has_value()) << db.error().detail;
    }
    const auto db = DatabaseManager::open(databasePath(), options({firstMigration()}));
    ASSERT_FALSE(db.has_value());
    EXPECT_EQ(db.error().code, ErrorCode::IncompatibleVersion);
}

TEST_F(DatabaseManagerTest, HeldWriteLockIsDatabaseLocked) {
    {
        const auto created = DatabaseManager::open(databasePath(), options());
        ASSERT_TRUE(created.has_value()) << created.error().detail;
    }
    const RawConnection other(databasePath());
    ASSERT_TRUE(run(other.database(), QStringLiteral("BEGIN IMMEDIATE")));
    {
        const auto blocked = DatabaseManager::open(databasePath(), options());
        ASSERT_FALSE(blocked.has_value());
        EXPECT_EQ(blocked.error().code, ErrorCode::DatabaseLocked) << blocked.error().detail;
    }
    ASSERT_TRUE(run(other.database(), QStringLiteral("ROLLBACK")));

    const auto reopened = DatabaseManager::open(databasePath(), options());
    EXPECT_TRUE(reopened.has_value()) << reopened.error().detail;
}

TEST_F(DatabaseManagerTest, ExclusiveLockBeforeWalIsDatabaseLocked) {
    const RawConnection other(databasePath());
    ASSERT_TRUE(run(other.database(), QStringLiteral("CREATE TABLE t (id INTEGER PRIMARY KEY)")));
    ASSERT_TRUE(run(other.database(), QStringLiteral("BEGIN EXCLUSIVE")));

    const auto blocked = DatabaseManager::open(databasePath(), options());
    ASSERT_FALSE(blocked.has_value());
    EXPECT_EQ(blocked.error().code, ErrorCode::DatabaseLocked) << blocked.error().detail;
    EXPECT_TRUE(run(other.database(), QStringLiteral("ROLLBACK")));
}

TEST_F(DatabaseManagerTest, MissingFolderIsIo) {
    const QString path = m_directory.filePath(QStringLiteral("no/such/folder/club.sqlite"));
    const auto db = DatabaseManager::open(path, options());
    ASSERT_FALSE(db.has_value());
    EXPECT_EQ(db.error().code, ErrorCode::Io) << db.error().detail;
}

TEST_F(DatabaseManagerTest, FolderInsteadOfFileIsIo) {
    ASSERT_TRUE(QDir().mkpath(databasePath()));
    const auto db = DatabaseManager::open(databasePath(), options());
    ASSERT_FALSE(db.has_value());
    EXPECT_EQ(db.error().code, ErrorCode::Io) << db.error().detail;
}

// --- backup() ---

TEST_F(DatabaseManagerTest, BackupOnInMemoryIsInternal) {
    const auto db = DatabaseManager::openInMemory();
    ASSERT_TRUE(db.has_value()) << db.error().detail;
    const auto backup = (*db)->backup("manual");
    ASSERT_FALSE(backup.has_value());
    EXPECT_EQ(backup.error().code, ErrorCode::Internal);
}

TEST_F(DatabaseManagerTest, BackupInsideTransactionIsInternal) {
    const auto db = DatabaseManager::open(databasePath(), options());
    ASSERT_TRUE(db.has_value()) << db.error().detail;
    auto transaction = Transaction::begin(**db);
    ASSERT_TRUE(transaction.has_value()) << transaction.error().detail;
    const auto backup = (*db)->backup("manual");
    ASSERT_FALSE(backup.has_value());
    EXPECT_EQ(backup.error().code, ErrorCode::Internal);
    EXPECT_TRUE(filesIn(backupDirectory()).isEmpty());
}

TEST_F(DatabaseManagerTest, OnDemandBackupPrunesToTheNewestTen) {
    const auto db = DatabaseManager::open(databasePath(), options());
    ASSERT_TRUE(db.has_value()) << db.error().detail;
    for (int i = 0; i < 12; ++i) {
        const auto backup = (*db)->backup("manual");
        ASSERT_TRUE(backup.has_value()) << backup.error().detail;
    }
    EXPECT_EQ(filesIn(backupDirectory()).size(), 10);
}

// --- Worker connections ---

TEST_F(DatabaseManagerTest, WorkerConnectionOnInMemoryIsInternal) {
    const auto db = DatabaseManager::openInMemory();
    ASSERT_TRUE(db.has_value()) << db.error().detail;
    const auto worker = (*db)->openWorkerConnection(QStringLiteral("worker"));
    ASSERT_FALSE(worker.has_value());
    EXPECT_EQ(worker.error().code, ErrorCode::Internal);
    EXPECT_FALSE(QSqlDatabase::contains(QStringLiteral("worker")));
}

TEST_F(DatabaseManagerTest, WorkerConnectionReadsOnItsOwnThreadWithThePragmas) {
    const auto db = DatabaseManager::open(databasePath(), options());
    ASSERT_TRUE(db.has_value()) << db.error().detail;
    ASSERT_TRUE(
        run((*db)->database(), QStringLiteral("INSERT INTO club (name) VALUES ('KS Test')")));

    std::thread worker([&db] {
        const QString name = QStringLiteral("worker");
        {
            const auto connection = (*db)->openWorkerConnection(name);
            ASSERT_TRUE(connection.has_value()) << connection.error().detail;
            EXPECT_EQ(scalar(*connection, QStringLiteral("SELECT name FROM club")).toString(),
                      QStringLiteral("KS Test"));
            EXPECT_EQ(scalar(*connection, QStringLiteral("PRAGMA foreign_keys")).toInt(), 1);
            EXPECT_EQ(scalar(*connection, QStringLiteral("PRAGMA busy_timeout")).toInt(), 100);
            QSqlDatabase(*connection).close();
        }
        QSqlDatabase::removeDatabase(name);
    });
    worker.join();
    EXPECT_FALSE(QSqlDatabase::contains(QStringLiteral("worker")));
}

TEST_F(DatabaseManagerTest, WorkerConnectionNameInUseIsConflict) {
    const auto db = DatabaseManager::open(databasePath(), options());
    ASSERT_TRUE(db.has_value()) << db.error().detail;
    const QString name = QStringLiteral("worker");
    {
        const auto first = (*db)->openWorkerConnection(name);
        ASSERT_TRUE(first.has_value()) << first.error().detail;
        const auto second = (*db)->openWorkerConnection(name);
        ASSERT_FALSE(second.has_value());
        EXPECT_EQ(second.error().code, ErrorCode::Conflict);
        QSqlDatabase(*first).close();
    }
    QSqlDatabase::removeDatabase(name);
}

// --- Connection cleanup: qml_smoke fails on any Qt warning, so this must stay silent ---

TEST_F(DatabaseManagerTest, OpenAndCloseLeaveNoQtWarnings) {
    g_qtMessages.clear();
    const QtMessageHandler previous = qInstallMessageHandler(collectProblems);
    {
        const auto file = DatabaseManager::open(databasePath(), options());
        EXPECT_TRUE(file.has_value());
        const auto memory = DatabaseManager::openInMemory();
        EXPECT_TRUE(memory.has_value());
    }
    const QString garbagePath = m_directory.filePath(QStringLiteral("garbage.sqlite"));
    ASSERT_TRUE(writeFile(garbagePath, QByteArray("not a database, just some text\n")));
    {
        const auto garbage = DatabaseManager::open(garbagePath, options());
        EXPECT_FALSE(garbage.has_value());
    }
    qInstallMessageHandler(previous);

    for (const QString& message : g_qtMessages) {
        ADD_FAILURE() << message.toStdString();
    }
}
