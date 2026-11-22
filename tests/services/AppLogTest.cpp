#include "services/AppLog.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using fdss::services::AppLog;

namespace {

class AppLogTest : public ::testing::Test {
protected:
    void TearDown() override { AppLog::uninstall(); }

    [[nodiscard]] QString read(const QString& name) const {
        QFile file(m_dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QString::fromUtf8(file.readAll());
    }

    [[nodiscard]] QStringList logFiles() const {
        return QDir(m_dir.path()).entryList({QStringLiteral("*.log")}, QDir::Files, QDir::Name);
    }

    QTemporaryDir m_dir;
};

} // namespace

TEST_F(AppLogTest, WritesMessagesWithTheirTypeToTheFile) {
    ASSERT_TRUE(m_dir.isValid());
    ASSERT_TRUE(AppLog::install(m_dir.path()).has_value());
    EXPECT_EQ(AppLog::currentFile(), m_dir.filePath(QStringLiteral("FootballDSS.log")));

    qWarning("first message for the log");
    qInfo("second message for the log");

    const QString text = read(QStringLiteral("FootballDSS.log"));
    EXPECT_TRUE(text.contains(QStringLiteral("[warning]"))) << text.toStdString();
    EXPECT_TRUE(text.contains(QStringLiteral("first message for the log")));
    EXPECT_TRUE(text.contains(QStringLiteral("[info]")));
    EXPECT_TRUE(text.contains(QStringLiteral("second message for the log")));
}

TEST_F(AppLogTest, EveryStartBeginsANewFileAndKeepsTheNewestOnes) {
    ASSERT_TRUE(m_dir.isValid());
    for (int start = 1; start <= 4; ++start) {
        ASSERT_TRUE(AppLog::install(m_dir.path(), 3).has_value());
        qInfo("start %d", start);
        AppLog::uninstall();
    }

    EXPECT_EQ(logFiles(),
              (QStringList{QStringLiteral("FootballDSS.1.log"), QStringLiteral("FootballDSS.2.log"),
                           QStringLiteral("FootballDSS.log")}));
    EXPECT_TRUE(read(QStringLiteral("FootballDSS.log")).contains(QStringLiteral("start 4")));
    EXPECT_TRUE(read(QStringLiteral("FootballDSS.1.log")).contains(QStringLiteral("start 3")));
    EXPECT_TRUE(read(QStringLiteral("FootballDSS.2.log")).contains(QStringLiteral("start 2")));
}

TEST_F(AppLogTest, AFullFileRotatesWhileRunning) {
    ASSERT_TRUE(m_dir.isValid());
    ASSERT_TRUE(AppLog::install(m_dir.path(), 2, 100).has_value());
    for (int line = 0; line < 20; ++line) {
        qInfo("a line long enough to fill the small file quickly, number %d", line);
    }

    EXPECT_EQ(logFiles().size(), 2);
    EXPECT_TRUE(read(QStringLiteral("FootballDSS.log")).contains(QStringLiteral("number 19")));
    EXPECT_FALSE(read(QStringLiteral("FootballDSS.log")).contains(QStringLiteral("number 0\n")));
}

TEST_F(AppLogTest, AfterUninstallNothingIsWritten) {
    ASSERT_TRUE(m_dir.isValid());
    ASSERT_TRUE(AppLog::install(m_dir.path()).has_value());
    AppLog::uninstall();

    qInfo("after the log is gone");

    EXPECT_TRUE(AppLog::currentFile().isEmpty());
    EXPECT_FALSE(read(QStringLiteral("FootballDSS.log")).contains(QStringLiteral("gone")));
}

TEST_F(AppLogTest, ASecondInstallIsRefused) {
    ASSERT_TRUE(m_dir.isValid());
    ASSERT_TRUE(AppLog::install(m_dir.path()).has_value());

    EXPECT_FALSE(AppLog::install(m_dir.path()).has_value());
}

TEST_F(AppLogTest, AFolderThatCannotBeWrittenIsAnIoError) {
    ASSERT_TRUE(m_dir.isValid());
    const auto installed = AppLog::install(m_dir.filePath(QStringLiteral("missing/folder")));

    ASSERT_FALSE(installed.has_value());
    EXPECT_EQ(installed.error().code, fdss::core::ErrorCode::Io);
    EXPECT_TRUE(AppLog::currentFile().isEmpty());
}

// P19 review: a refused start adds to the log of the running instance, without rotating it.
TEST_F(AppLogTest, AppendKeepsTheFileAndRotatesNothing) {
    ASSERT_TRUE(m_dir.isValid());
    ASSERT_TRUE(AppLog::install(m_dir.path()).has_value());
    qInfo("from the running instance");
    AppLog::uninstall();

    ASSERT_TRUE(
        AppLog::install(m_dir.path(), AppLog::kKeepFiles, AppLog::kMaxBytes, AppLog::Start::Append)
            .has_value());
    qCritical("start refused");

    EXPECT_EQ(logFiles(), QStringList{QStringLiteral("FootballDSS.log")});
    const QString text = read(QStringLiteral("FootballDSS.log"));
    EXPECT_TRUE(text.contains(QStringLiteral("from the running instance")));
    EXPECT_TRUE(text.contains(QStringLiteral("start refused")));
}

// P19 review: a log that cannot be moved away is continued, never emptied, and says so.
TEST_F(AppLogTest, AFailedRotationAppendsInsteadOfTruncating) {
    ASSERT_TRUE(m_dir.isValid());
    ASSERT_TRUE(AppLog::install(m_dir.path(), 2).has_value());
    qInfo("written before the rotation");
    AppLog::uninstall();
    // A folder where the rotation wants to put the old file: neither remove() nor rename() works.
    ASSERT_TRUE(QDir(m_dir.path()).mkdir(QStringLiteral("FootballDSS.1.log")));

    ASSERT_TRUE(AppLog::install(m_dir.path(), 2).has_value());
    qInfo("written after the failed rotation");

    const QString text = read(QStringLiteral("FootballDSS.log"));
    EXPECT_TRUE(text.contains(QStringLiteral("written before the rotation"))) << text.toStdString();
    EXPECT_TRUE(text.contains(QStringLiteral("written after the failed rotation")));
    EXPECT_TRUE(text.contains(QStringLiteral("[warning]")));
    EXPECT_TRUE(text.contains(QStringLiteral("cannot rotate")));
}

TEST_F(AppLogTest, AFailedRotationWhileRunningKeepsEveryLine) {
    ASSERT_TRUE(m_dir.isValid());
    ASSERT_TRUE(QDir(m_dir.path()).mkdir(QStringLiteral("FootballDSS.1.log")));
    ASSERT_TRUE(AppLog::install(m_dir.path(), 2, 100).has_value());
    for (int line = 0; line < 10; ++line) {
        qInfo("a line long enough to fill the small file quickly, number %d", line);
    }

    const QString text = read(QStringLiteral("FootballDSS.log"));
    EXPECT_TRUE(text.contains(QStringLiteral("number 0\n"))) << text.toStdString();
    EXPECT_TRUE(text.contains(QStringLiteral("number 9\n")));
    EXPECT_EQ(text.count(QStringLiteral("cannot rotate")), 1);
}
