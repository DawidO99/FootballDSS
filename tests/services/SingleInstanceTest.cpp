#include "services/SingleInstance.hpp"

#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using fdss::core::ErrorCode;
using fdss::services::SingleInstance;

TEST(SingleInstanceTest, TheFirstInstanceGetsTheLock) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const auto first = SingleInstance::acquire(dir.path());

    ASSERT_TRUE(first.has_value()) << first.error().detail;
    EXPECT_TRUE(QFile::exists(dir.filePath(QStringLiteral("FootballDSS.lock"))));
}

TEST(SingleInstanceTest, ASecondInstanceIsAConflictUntilTheFirstEnds) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto first = SingleInstance::acquire(dir.path());
    ASSERT_TRUE(first.has_value()) << first.error().detail;

    const auto second = SingleInstance::acquire(dir.path());
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, ErrorCode::Conflict);

    first->reset();
    EXPECT_FALSE(QFile::exists(dir.filePath(QStringLiteral("FootballDSS.lock"))));
    EXPECT_TRUE(SingleInstance::acquire(dir.path()).has_value());
}

TEST(SingleInstanceTest, ALockLeftByADeadProcessIsTakenOver) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    // The format QLockFile writes: pid, application name, host name. No process has pid 2^31 - 2.
    QFile stale(dir.filePath(QStringLiteral("FootballDSS.lock")));
    ASSERT_TRUE(stale.open(QIODevice::WriteOnly));
    stale.write("2147483646\nFootballDSS\n\n");
    stale.close();

    EXPECT_TRUE(SingleInstance::acquire(dir.path()).has_value());
}

TEST(SingleInstanceTest, AFolderThatCannotBeWrittenIsAnIoError) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const auto lock = SingleInstance::acquire(dir.filePath(QStringLiteral("missing/folder")));

    ASSERT_FALSE(lock.has_value());
    EXPECT_EQ(lock.error().code, ErrorCode::Io);
}
