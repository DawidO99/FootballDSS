#include "data/db/AppPaths.hpp"

#include <QDir>
#include <QFileInfo>

#include <gtest/gtest.h>

using namespace fdss::data;

TEST(AppPathsTest, DatabasePathIsClubSqliteInAnExistingFolder) {
    const auto path = AppPaths::defaultDatabasePath();
    ASSERT_TRUE(path.has_value()) << path.error().detail;
    const QFileInfo info(*path);
    EXPECT_EQ(info.fileName(), QStringLiteral("club.sqlite"));
    EXPECT_TRUE(info.absoluteDir().exists());
}

TEST(AppPathsTest, BackupDirectoryExistsNextToTheDatabase) {
    const auto database = AppPaths::defaultDatabasePath();
    const auto backups = AppPaths::backupDirectory();
    ASSERT_TRUE(database.has_value()) << database.error().detail;
    ASSERT_TRUE(backups.has_value()) << backups.error().detail;
    EXPECT_TRUE(QDir(*backups).exists());
    EXPECT_EQ(*backups, QFileInfo(*database).absoluteDir().filePath(QStringLiteral("backups")));
}

TEST(AppPathsTest, LogDirectoryExistsNextToTheDatabase) {
    const auto database = AppPaths::defaultDatabasePath();
    const auto logs = AppPaths::logDirectory();
    ASSERT_TRUE(database.has_value()) << database.error().detail;
    ASSERT_TRUE(logs.has_value()) << logs.error().detail;
    EXPECT_TRUE(QDir(*logs).exists());
    EXPECT_EQ(*logs, QFileInfo(*database).absoluteDir().filePath(QStringLiteral("logs")));
}

TEST(AppPathsTest, TestModeKeepsTestsAwayFromTheRealAppData) {
    const auto path = AppPaths::defaultDatabasePath();
    ASSERT_TRUE(path.has_value()) << path.error().detail;
    EXPECT_TRUE(path->contains(QStringLiteral("qttest"))) << path->toStdString();
}
