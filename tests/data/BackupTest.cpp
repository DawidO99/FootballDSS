#include "data/db/Backup.hpp"

#include "TestDatabase.hpp"
#include "data/db/DatabaseManager.hpp"

#include <QDate>
#include <QDateTime>
#include <QFileInfo>
#include <QTime>

#include <gtest/gtest.h>

using namespace fdss::data;
using namespace fdss::tests;
using fdss::core::ErrorCode;

namespace {

const QDateTime kNoon(QDate(2026, 11, 1), QTime(12, 0, 0));

class BackupTest : public TempDatabaseTest {
protected:
    void SetUp() override {
        TempDatabaseTest::SetUp();
        auto opened = DatabaseManager::open(databasePath(), options());
        ASSERT_TRUE(opened.has_value()) << opened.error().detail;
        m_db = std::move(*opened);
    }

    [[nodiscard]] QString backupPath(const QString& name) const {
        return QDir(backupDirectory()).filePath(name);
    }

    // Empty file with a given name: pruning looks at names only.
    void touchBackup(const QString& name) const {
        ASSERT_TRUE(QDir().mkpath(backupDirectory()));
        ASSERT_TRUE(writeFile(backupPath(name), QByteArray{}));
    }

    std::unique_ptr<DatabaseManager> m_db;
};

} // namespace

TEST_F(BackupTest, WritesAReadableCopyWithTheAgreedName) {
    ASSERT_TRUE(
        run(m_db->database(), QStringLiteral("INSERT INTO club (name) VALUES ('KS Test')")));

    const auto path = createBackup(m_db->database(), backupDirectory(), "manual", kNoon);
    ASSERT_TRUE(path.has_value()) << path.error().detail;
    EXPECT_EQ(QFileInfo(*path).fileName(), QStringLiteral("club-20261101-120000-manual.sqlite"));

    const RawConnection backup(*path);
    EXPECT_EQ(scalar(backup.database(), QStringLiteral("SELECT name FROM club")).toString(),
              QStringLiteral("KS Test"));
    EXPECT_EQ(
        scalar(backup.database(), QStringLiteral("SELECT version FROM schema_version")).toInt(), 1);
    EXPECT_EQ(scalar(backup.database(), QStringLiteral("PRAGMA integrity_check")).toString(),
              QStringLiteral("ok"));
}

TEST_F(BackupTest, ContainsChangesStillInTheWal) {
    ASSERT_EQ(scalar(m_db->database(), QStringLiteral("PRAGMA journal_mode")).toString(),
              QStringLiteral("wal"));
    ASSERT_TRUE(run(m_db->database(), QStringLiteral("INSERT INTO club (name) VALUES ('W WAL')")));

    const auto path = createBackup(m_db->database(), backupDirectory(), "manual", kNoon);
    ASSERT_TRUE(path.has_value()) << path.error().detail;
    const RawConnection backup(*path);
    EXPECT_EQ(scalar(backup.database(), QStringLiteral("SELECT count(*) FROM club")).toInt(), 1);
}

TEST_F(BackupTest, BackupsInTheSameSecondGetSuffixes) {
    const auto first = createBackup(m_db->database(), backupDirectory(), "manual", kNoon);
    const auto second = createBackup(m_db->database(), backupDirectory(), "manual", kNoon);
    const auto third = createBackup(m_db->database(), backupDirectory(), "manual", kNoon);
    ASSERT_TRUE(first.has_value()) << first.error().detail;
    ASSERT_TRUE(second.has_value()) << second.error().detail;
    ASSERT_TRUE(third.has_value()) << third.error().detail;
    EXPECT_EQ(QFileInfo(*second).fileName(),
              QStringLiteral("club-20261101-120000-manual-2.sqlite"));
    EXPECT_EQ(QFileInfo(*third).fileName(), QStringLiteral("club-20261101-120000-manual-3.sqlite"));
    EXPECT_EQ(filesIn(backupDirectory()).size(), 3);
}

TEST_F(BackupTest, InvalidReasonIsRefusedBeforeAnythingIsWritten) {
    for (const char* reason :
         {"", "../evil", "a b", "UPPER", "with-dash", "kopia.zapasowa", "żółć"}) {
        SCOPED_TRACE(reason);
        const auto path = createBackup(m_db->database(), backupDirectory(), reason, kNoon);
        ASSERT_FALSE(path.has_value());
        EXPECT_EQ(path.error().code, ErrorCode::InvalidInput);
        EXPECT_EQ(path.error().field, "reason");
    }
    EXPECT_FALSE(QFileInfo::exists(backupDirectory()));
}

TEST_F(BackupTest, UnusableFolderIsIo) {
    const QString blocked = m_directory.filePath(QStringLiteral("not-a-folder"));
    ASSERT_TRUE(writeFile(blocked, QByteArray("x")));
    const auto path = createBackup(m_db->database(), blocked, "manual", kNoon);
    ASSERT_FALSE(path.has_value());
    EXPECT_EQ(path.error().code, ErrorCode::Io);
}

TEST_F(BackupTest, FailedBackupLeavesNoFile) {
    ASSERT_TRUE(run(m_db->database(), QStringLiteral("BEGIN IMMEDIATE")));
    const auto path = createBackup(m_db->database(), backupDirectory(), "manual", kNoon);
    EXPECT_FALSE(path.has_value());
    EXPECT_TRUE(filesIn(backupDirectory()).isEmpty());
    EXPECT_TRUE(run(m_db->database(), QStringLiteral("ROLLBACK")));
}

TEST_F(BackupTest, PruningKeepsTheNewestByTimestamp) {
    for (int day = 1; day <= 12; ++day) {
        touchBackup(
            QStringLiteral("club-202611%1-120000-manual.sqlite").arg(day, 2, 10, QLatin1Char('0')));
    }
    ASSERT_TRUE(pruneBackups(backupDirectory()).has_value());

    const QStringList left = filesIn(backupDirectory());
    EXPECT_EQ(left.size(), kBackupsToKeep);
    EXPECT_FALSE(left.contains(QStringLiteral("club-20261101-120000-manual.sqlite")));
    EXPECT_FALSE(left.contains(QStringLiteral("club-20261102-120000-manual.sqlite")));
    EXPECT_TRUE(left.contains(QStringLiteral("club-20261103-120000-manual.sqlite")));
    EXPECT_TRUE(left.contains(QStringLiteral("club-20261112-120000-manual.sqlite")));
}

// As plain file names "-10" sorts before "-2", and "-2.sqlite" before ".sqlite".
TEST_F(BackupTest, PruningOrdersSuffixesNumerically) {
    for (int i = 0; i < 12; ++i) {
        const auto path = createBackup(m_db->database(), backupDirectory(), "manual", kNoon);
        ASSERT_TRUE(path.has_value()) << path.error().detail;
    }
    ASSERT_TRUE(pruneBackups(backupDirectory()).has_value());

    const QStringList left = filesIn(backupDirectory());
    EXPECT_EQ(left.size(), kBackupsToKeep);
    EXPECT_FALSE(left.contains(QStringLiteral("club-20261101-120000-manual.sqlite")));
    EXPECT_FALSE(left.contains(QStringLiteral("club-20261101-120000-manual-2.sqlite")));
    EXPECT_TRUE(left.contains(QStringLiteral("club-20261101-120000-manual-3.sqlite")));
    EXPECT_TRUE(left.contains(QStringLiteral("club-20261101-120000-manual-10.sqlite")));
    EXPECT_TRUE(left.contains(QStringLiteral("club-20261101-120000-manual-12.sqlite")));
}

TEST_F(BackupTest, PruningNeverDeletesOtherFiles) {
    const QStringList others{QStringLiteral("notatki.txt"),
                             QStringLiteral("club.sqlite"),
                             QStringLiteral("club-copy.sqlite"),
                             QStringLiteral("club-20200101-120000-UPPER.sqlite"),
                             QStringLiteral("club-2020-120000-manual.sqlite"),
                             QStringLiteral("club-20200101-120000-manual.sqlite.bak"),
                             QStringLiteral("old-club-20200101-120000-manual.sqlite")};
    for (const QString& name : others) {
        touchBackup(name);
    }
    for (int day = 1; day <= 12; ++day) {
        touchBackup(
            QStringLiteral("club-202611%1-120000-manual.sqlite").arg(day, 2, 10, QLatin1Char('0')));
    }
    ASSERT_TRUE(pruneBackups(backupDirectory()).has_value());

    const QStringList left = filesIn(backupDirectory());
    EXPECT_EQ(left.size(), kBackupsToKeep + others.size());
    for (const QString& name : others) {
        EXPECT_TRUE(left.contains(name)) << name.toStdString();
    }
}

TEST_F(BackupTest, PruningWithFewFilesDeletesNothing) {
    touchBackup(QStringLiteral("club-20261101-120000-manual.sqlite"));
    ASSERT_TRUE(pruneBackups(backupDirectory()).has_value());
    EXPECT_EQ(filesIn(backupDirectory()).size(), 1);
}

TEST_F(BackupTest, PruningMissingFolderIsNotAnError) {
    EXPECT_TRUE(pruneBackups(m_directory.filePath(QStringLiteral("nothing-here"))).has_value());
}
