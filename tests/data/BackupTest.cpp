#include "data/db/Backup.hpp"

#include "TestDatabase.hpp"
#include "data/db/DatabaseManager.hpp"

#include <QDate>
#include <QDateTime>
#include <QFileInfo>
#include <QTime>

#include <gtest/gtest.h>

#include <array>
#include <barrier>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

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

    // Rows of the club table in a backup, to tell whether it holds the data of its moment.
    [[nodiscard]] static int clubRowsIn(const QString& path) {
        const RawConnection backup(path);
        return scalar(backup.database(), QStringLiteral("SELECT count(*) FROM club")).toInt();
    }

    [[nodiscard]] ::testing::AssertionResult addClub() const {
        return run(m_db->database(), QStringLiteral("INSERT INTO club (name) VALUES ('KS Test')"));
    }

    std::unique_ptr<DatabaseManager> m_db;
};

} // namespace

// Every reported backup must exist afterwards: two calls must never pick the same file, or the
// loser of the race deletes the file of the winner.
TEST_F(BackupTest, ConcurrentBackupsNeverLoseACopy) {
    constexpr int kRounds = 20;
    constexpr int kThreads = 2;
    std::barrier sameMoment(kThreads);
    std::array<std::vector<QString>, kThreads> paths;

    const auto work = [&](int index) {
        const QString name = QStringLiteral("backup_worker_%1").arg(index);
        {
            const auto connection = m_db->openWorkerConnection(name);
            EXPECT_TRUE(connection.has_value()) << connection.error().detail;
            for (int round = 0; round < kRounds; ++round) {
                sameMoment.arrive_and_wait();
                if (!connection) {
                    continue;
                }
                const auto path = createBackup(*connection, backupDirectory(), "manual", kNoon);
                EXPECT_TRUE(path.has_value()) << path.error().detail;
                if (path) {
                    paths[static_cast<std::size_t>(index)].push_back(*path);
                }
            }
            if (connection) {
                QSqlDatabase(*connection).close();
            }
        }
        QSqlDatabase::removeDatabase(name);
    };
    std::thread first(work, 0);
    std::thread second(work, 1);
    first.join();
    second.join();

    QStringList all;
    for (const std::vector<QString>& ofThread : paths) {
        for (const QString& path : ofThread) {
            EXPECT_TRUE(QFileInfo::exists(path)) << path.toStdString();
            all.append(path);
        }
    }
    EXPECT_EQ(all.size(), kRounds * kThreads);
    EXPECT_EQ(all.removeDuplicates(), 0);
    EXPECT_EQ(filesIn(backupDirectory()).size(), kRounds * kThreads);
    for (const QString& path : std::as_const(all)) {
        const RawConnection backup(path);
        EXPECT_EQ(scalar(backup.database(), QStringLiteral("PRAGMA integrity_check")).toString(),
                  QStringLiteral("ok"))
            << path.toStdString();
    }
}

TEST_F(BackupTest, EveryBackupOfOneSecondSurvivesPruningAndHoldsCurrentData) {
    for (int i = 1; i <= 15; ++i) {
        ASSERT_TRUE(addClub());
        const auto path = createBackup(m_db->database(), backupDirectory(), "manual", kNoon);
        ASSERT_TRUE(path.has_value()) << path.error().detail;
        ASSERT_TRUE(pruneBackups(backupDirectory(), *path).has_value());
        ASSERT_TRUE(QFileInfo::exists(*path)) << "backup " << i << ": " << path->toStdString();
        EXPECT_EQ(clubRowsIn(*path), i) << path->toStdString();
    }
    const QStringList left = filesIn(backupDirectory());
    EXPECT_EQ(left.size(), kBackupsToKeep);
    EXPECT_FALSE(left.contains(QStringLiteral("club-20261101-120000-manual-5.sqlite")));
    EXPECT_TRUE(left.contains(QStringLiteral("club-20261101-120000-manual-6.sqlite")));
    EXPECT_TRUE(left.contains(QStringLiteral("club-20261101-120000-manual-15.sqlite")));
}

TEST_F(BackupTest, SuffixContinuesAfterTheHighestOfThatSecond) {
    touchBackup(QStringLiteral("club-20261101-120000-manual-5.sqlite"));
    touchBackup(QStringLiteral("club-20261101-120000-import-7.sqlite"));  // another reason
    touchBackup(QStringLiteral("club-20261101-120001-manual-99.sqlite")); // another second

    const auto path = createBackup(m_db->database(), backupDirectory(), "manual", kNoon);
    ASSERT_TRUE(path.has_value()) << path.error().detail;
    EXPECT_EQ(QFileInfo(*path).fileName(), QStringLiteral("club-20261101-120000-manual-8.sqlite"));
}

TEST_F(BackupTest, HighestAllowedSuffixIsStillUsed) {
    touchBackup(QStringLiteral("club-20261101-120000-manual-999998.sqlite"));

    const auto path = createBackup(m_db->database(), backupDirectory(), "manual", kNoon);
    ASSERT_TRUE(path.has_value()) << path.error().detail;
    EXPECT_EQ(QFileInfo(*path).fileName(),
              QStringLiteral("club-20261101-120000-manual-999999.sqlite"));
}

TEST_F(BackupTest, SuffixBeyondTheLimitIsInternalAndCreatesNoFile) {
    const QString highest = QStringLiteral("club-20261101-120000-manual-999999.sqlite");
    touchBackup(highest);

    const auto path = createBackup(m_db->database(), backupDirectory(), "manual", kNoon);
    ASSERT_FALSE(path.has_value());
    EXPECT_EQ(path.error().code, ErrorCode::Internal);
    EXPECT_EQ(filesIn(backupDirectory()), QStringList{highest});
}

TEST_F(BackupTest, SevenDigitSuffixIsNotABackup) {
    touchBackup(QStringLiteral("club-20261101-120000-manual-1000000.sqlite"));

    const auto path = createBackup(m_db->database(), backupDirectory(), "manual", kNoon);
    ASSERT_TRUE(path.has_value()) << path.error().detail;
    EXPECT_EQ(QFileInfo(*path).fileName(), QStringLiteral("club-20261101-120000-manual.sqlite"));
}

TEST_F(BackupTest, NameReservedByAnotherCallIsNotTouched) {
    // An empty file is what a call running right now has just reserved.
    touchBackup(QStringLiteral("club-20261101-120000-manual.sqlite"));

    const auto path = createBackup(m_db->database(), backupDirectory(), "manual", kNoon);
    ASSERT_TRUE(path.has_value()) << path.error().detail;
    EXPECT_EQ(QFileInfo(*path).fileName(), QStringLiteral("club-20261101-120000-manual-2.sqlite"));
    EXPECT_TRUE(
        readFile(backupPath(QStringLiteral("club-20261101-120000-manual.sqlite"))).isEmpty());
}

// The reservation relies on this SQLite behaviour.
TEST_F(BackupTest, VacuumIntoAcceptsAnEmptyExistingFile) {
    ASSERT_TRUE(addClub());
    const QString path = m_directory.filePath(QStringLiteral("empty.sqlite"));
    ASSERT_TRUE(writeFile(path, QByteArray{}));

    EXPECT_TRUE(run(m_db->database(), QStringLiteral("VACUUM INTO ?"), {path}));
    EXPECT_EQ(clubRowsIn(path), 1);
}

TEST_F(BackupTest, VacuumIntoRefusesAFileWithContent) {
    const QString path = m_directory.filePath(QStringLiteral("taken.sqlite"));
    const QByteArray content("not empty");
    ASSERT_TRUE(writeFile(path, content));

    EXPECT_FALSE(run(m_db->database(), QStringLiteral("VACUUM INTO ?"), {path}));
    EXPECT_EQ(readFile(path), content);
}

TEST_F(BackupTest, FailedBackupRemovesOnlyItsOwnFile) {
    ASSERT_TRUE(addClub());
    const auto earlier = createBackup(m_db->database(), backupDirectory(), "manual", kNoon);
    ASSERT_TRUE(earlier.has_value()) << earlier.error().detail;

    ASSERT_TRUE(run(m_db->database(), QStringLiteral("BEGIN IMMEDIATE")));
    const auto failed = createBackup(m_db->database(), backupDirectory(), "manual", kNoon);
    EXPECT_FALSE(failed.has_value());
    ASSERT_TRUE(run(m_db->database(), QStringLiteral("ROLLBACK")));

    EXPECT_EQ(filesIn(backupDirectory()), QStringList{QFileInfo(*earlier).fileName()});
    EXPECT_EQ(clubRowsIn(*earlier), 1);
}

// The clock was moved back: the new backup has the oldest name of all.
TEST_F(BackupTest, PruningNeverDeletesTheBackupJustMade) {
    for (int day = 2; day <= 11; ++day) {
        touchBackup(
            QStringLiteral("club-202611%1-120000-manual.sqlite").arg(day, 2, 10, QLatin1Char('0')));
    }
    const auto path = createBackup(m_db->database(), backupDirectory(), "manual", kNoon);
    ASSERT_TRUE(path.has_value()) << path.error().detail;
    ASSERT_TRUE(pruneBackups(backupDirectory(), *path).has_value());

    const QStringList left = filesIn(backupDirectory());
    EXPECT_TRUE(QFileInfo::exists(*path));
    EXPECT_EQ(left.size(), kBackupsToKeep);
    EXPECT_FALSE(left.contains(QStringLiteral("club-20261102-120000-manual.sqlite")));
    EXPECT_TRUE(left.contains(QStringLiteral("club-20261103-120000-manual.sqlite")));
}

TEST_F(BackupTest, BackupOfTheManagerSurvivesItsOwnPruning) {
    for (int i = 1; i <= 15; ++i) {
        ASSERT_TRUE(addClub());
        const auto path = m_db->backup("manual");
        ASSERT_TRUE(path.has_value()) << path.error().detail;
        ASSERT_TRUE(QFileInfo::exists(*path)) << "backup " << i << ": " << path->toStdString();
        EXPECT_EQ(clubRowsIn(*path), i) << path->toStdString();
    }
    EXPECT_EQ(filesIn(backupDirectory()).size(), kBackupsToKeep);
}

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
        scalar(backup.database(), QStringLiteral("SELECT version FROM schema_version")).toInt(), 3);
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
