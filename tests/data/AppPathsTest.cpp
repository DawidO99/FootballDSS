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

TEST(AppPathsTest, TestModeKeepsTestsAwayFromTheRealAppData) {
    const auto path = AppPaths::defaultDatabasePath();
    ASSERT_TRUE(path.has_value()) << path.error().detail;
    EXPECT_TRUE(path->contains(QStringLiteral("qttest"))) << path->toStdString();
}
