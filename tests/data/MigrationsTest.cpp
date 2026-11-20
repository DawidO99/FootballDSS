#include "data/db/Migrations.hpp"

#include "TestDatabase.hpp"
#include "data/db/DatabaseManager.hpp"

#include <QFileInfo>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace fdss::data;
using namespace fdss::tests;
using fdss::core::ErrorCode;

namespace {

class MigrationsTest : public TempDatabaseTest {};

std::vector<QString> split(const char* sql) { return splitSqlStatements(QString::fromUtf8(sql)); }

} // namespace

// --- splitSqlStatements ---

TEST(SplitSqlStatementsTest, OneStatementPerSemicolonAtLineEnd) {
    const auto statements =
        split("CREATE TABLE a (\n  id INTEGER\n);\nCREATE TABLE b (id INTEGER);\n");
    ASSERT_EQ(statements.size(), 2U);
    EXPECT_EQ(statements[0], QStringLiteral("CREATE TABLE a (\nid INTEGER\n);"));
    EXPECT_EQ(statements[1], QStringLiteral("CREATE TABLE b (id INTEGER);"));
}

TEST(SplitSqlStatementsTest, AcceptsCrlfLineEndings) {
    const auto statements =
        split("CREATE TABLE a (id INTEGER);\r\nCREATE TABLE b (id INTEGER);\r\n");
    ASSERT_EQ(statements.size(), 2U);
    EXPECT_EQ(statements[0], QStringLiteral("CREATE TABLE a (id INTEGER);"));
    EXPECT_EQ(statements[1], QStringLiteral("CREATE TABLE b (id INTEGER);"));
}

TEST(SplitSqlStatementsTest, DropsCommentLinesEvenWithASemicolon) {
    const auto statements =
        split("-- first comment;\nCREATE TABLE a (id INTEGER);\n   -- last one;\n");
    ASSERT_EQ(statements.size(), 1U);
    EXPECT_EQ(statements[0], QStringLiteral("CREATE TABLE a (id INTEGER);"));
}

TEST(SplitSqlStatementsTest, KeepsAStatementWithoutTheFinalSemicolon) {
    const auto statements = split("CREATE TABLE a (id INTEGER);\nCREATE TABLE b (id INTEGER)");
    ASSERT_EQ(statements.size(), 2U);
    EXPECT_EQ(statements[1], QStringLiteral("CREATE TABLE b (id INTEGER)"));
}

TEST(SplitSqlStatementsTest, DoesNotSplitInsideALine) {
    const auto statements = split("SELECT 1; SELECT 2;\n");
    ASSERT_EQ(statements.size(), 1U);
    EXPECT_EQ(statements[0], QStringLiteral("SELECT 1; SELECT 2;"));
}

TEST(SplitSqlStatementsTest, NothingToRunGivesNoStatements) {
    EXPECT_TRUE(split("").empty());
    EXPECT_TRUE(split("\n  \n-- only a comment\n").empty());
}

// --- embeddedMigrations ---

TEST(EmbeddedMigrationsTest, StartAtOneAndHaveStatements) {
    const auto migrations = embeddedMigrations();
    ASSERT_TRUE(migrations.has_value()) << migrations.error().detail;
    ASSERT_FALSE(migrations->empty());
    EXPECT_EQ(migrations->front().version, 1);
    EXPECT_EQ(migrations->front().name, QStringLiteral("initial"));
    EXPECT_FALSE(splitSqlStatements(migrations->front().sql).empty());
    ASSERT_GE(migrations->size(), 2U);
    EXPECT_EQ(migrations->at(1).version, 2);
    EXPECT_EQ(migrations->at(1).name, QStringLiteral("match_live"));
}

// --- Runner ---

TEST_F(MigrationsTest, AppliesAllPendingInOrder) {
    const auto db =
        DatabaseManager::open(databasePath(), options({firstMigration(), secondMigration()}));
    ASSERT_TRUE(db.has_value()) << db.error().detail;
    EXPECT_EQ((*db)->schemaVersion(), 2);
    EXPECT_TRUE(tableExists((*db)->database(), QStringLiteral("a")));
    EXPECT_TRUE(tableExists((*db)->database(), QStringLiteral("b")));
    EXPECT_EQ(
        scalar((*db)->database(), QStringLiteral("SELECT version FROM schema_version")).toInt(), 2);
    EXPECT_EQ(
        scalar((*db)->database(), QStringLiteral("SELECT count(*) FROM schema_version")).toInt(),
        1);
}

TEST_F(MigrationsTest, BrandNewFileGetsNoBackupEvenWithSeveralMigrations) {
    const auto db =
        DatabaseManager::open(databasePath(), options({firstMigration(), secondMigration()}));
    ASSERT_TRUE(db.has_value()) << db.error().detail;
    EXPECT_TRUE(filesIn(backupDirectory()).isEmpty());
}

TEST_F(MigrationsTest, UpgradeBacksUpTheOldVersionFirst) {
    {
        const auto db = DatabaseManager::open(databasePath(), options({firstMigration()}));
        ASSERT_TRUE(db.has_value()) << db.error().detail;
        ASSERT_TRUE(run((*db)->database(), QStringLiteral("INSERT INTO a (id) VALUES (42)")));
    }

    const auto upgraded =
        DatabaseManager::open(databasePath(), options({firstMigration(), secondMigration()}));
    ASSERT_TRUE(upgraded.has_value()) << upgraded.error().detail;
    EXPECT_EQ((*upgraded)->schemaVersion(), 2);

    const QStringList backups = filesIn(backupDirectory());
    ASSERT_EQ(backups.size(), 1);
    EXPECT_TRUE(backups.first().contains(QStringLiteral("pre_migration")))
        << backups.first().toStdString();

    const RawConnection backup(QDir(backupDirectory()).filePath(backups.first()));
    EXPECT_EQ(
        scalar(backup.database(), QStringLiteral("SELECT version FROM schema_version")).toInt(), 1);
    EXPECT_EQ(scalar(backup.database(), QStringLiteral("SELECT id FROM a")).toInt(), 42);
    EXPECT_FALSE(tableExists(backup.database(), QStringLiteral("b")));
}

TEST_F(MigrationsTest, FailingMigrationRollsBackAndNamesItsNumber) {
    {
        const auto db = DatabaseManager::open(databasePath(), options({firstMigration()}));
        ASSERT_TRUE(db.has_value()) << db.error().detail;
    }
    {
        const auto failed =
            DatabaseManager::open(databasePath(), options({firstMigration(), failingMigration()}));
        ASSERT_FALSE(failed.has_value());
        EXPECT_EQ(failed.error().code, ErrorCode::Database);
        EXPECT_NE(failed.error().detail.find("migration 2"), std::string::npos)
            << failed.error().detail;
    }

    // Still usable at the previous version, without the table the migration had already created.
    const auto reopened = DatabaseManager::open(databasePath(), options({firstMigration()}));
    ASSERT_TRUE(reopened.has_value()) << reopened.error().detail;
    EXPECT_EQ((*reopened)->schemaVersion(), 1);
    EXPECT_FALSE(tableExists((*reopened)->database(), QStringLiteral("b")));
}

TEST_F(MigrationsTest, MigrationsBeforeTheFailingOneStayApplied) {
    const auto failed =
        DatabaseManager::open(databasePath(), options({firstMigration(), failingMigration()}));
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, ErrorCode::Database);

    const RawConnection raw(databasePath());
    EXPECT_EQ(scalar(raw.database(), QStringLiteral("SELECT version FROM schema_version")).toInt(),
              1);
    EXPECT_FALSE(tableExists(raw.database(), QStringLiteral("b")));
}

TEST_F(MigrationsTest, TwoStatementsOnOneLineFailInsteadOfRunningPartly) {
    const Migration twoOnOneLine{
        1, QStringLiteral("two"),
        QStringLiteral("CREATE TABLE schema_version (id INTEGER PRIMARY "
                       "KEY, version INTEGER); CREATE TABLE a (id INTEGER);\n")};
    const auto db = DatabaseManager::open(databasePath(), options({twoOnOneLine}));
    ASSERT_FALSE(db.has_value());
    EXPECT_EQ(db.error().code, ErrorCode::Database);

    const RawConnection raw(databasePath());
    EXPECT_FALSE(tableExists(raw.database(), QStringLiteral("schema_version")));
}

TEST_F(MigrationsTest, NoMigrationsIsInternal) {
    const auto db = DatabaseManager::open(databasePath(), options(std::vector<Migration>{}));
    ASSERT_FALSE(db.has_value());
    EXPECT_EQ(db.error().code, ErrorCode::Internal);
}

TEST_F(MigrationsTest, GapInVersionsIsInternal) {
    Migration third = secondMigration();
    third.version = 3;
    const auto db = DatabaseManager::open(databasePath(), options({firstMigration(), third}));
    ASSERT_FALSE(db.has_value());
    EXPECT_EQ(db.error().code, ErrorCode::Internal);
}

TEST_F(MigrationsTest, DuplicateVersionIsInternal) {
    Migration duplicate = secondMigration();
    duplicate.version = 1;
    const auto db = DatabaseManager::open(databasePath(), options({firstMigration(), duplicate}));
    ASSERT_FALSE(db.has_value());
    EXPECT_EQ(db.error().code, ErrorCode::Internal);
}

TEST_F(MigrationsTest, BackupFailureStopsTheMigration) {
    {
        const auto db = DatabaseManager::open(databasePath(), options({firstMigration()}));
        ASSERT_TRUE(db.has_value()) << db.error().detail;
    }
    // A file where the backup folder should be.
    const QString blocked = m_directory.filePath(QStringLiteral("not-a-folder"));
    ASSERT_TRUE(writeFile(blocked, QByteArray("x")));
    OpenOptions opts = options({firstMigration(), secondMigration()});
    opts.backupDirectory = blocked;

    {
        const auto db = DatabaseManager::open(databasePath(), opts);
        ASSERT_FALSE(db.has_value());
        EXPECT_EQ(db.error().code, ErrorCode::Io) << db.error().detail;
    }
    const RawConnection raw(databasePath());
    EXPECT_EQ(scalar(raw.database(), QStringLiteral("SELECT version FROM schema_version")).toInt(),
              1);
    EXPECT_FALSE(tableExists(raw.database(), QStringLiteral("b")));
}

TEST_F(MigrationsTest, TableWithoutVersionRowIsNotADatabase) {
    {
        const RawConnection raw(databasePath());
        ASSERT_TRUE(
            run(raw.database(), QStringLiteral("CREATE TABLE schema_version (version INTEGER)")));
    }
    const auto db = DatabaseManager::open(databasePath(), options());
    ASSERT_FALSE(db.has_value());
    EXPECT_EQ(db.error().code, ErrorCode::NotADatabase) << db.error().detail;
}
