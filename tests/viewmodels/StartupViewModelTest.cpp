#include "viewmodels/StartupViewModel.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using fdss::core::Error;
using fdss::core::ErrorCode;
using fdss::viewmodels::StartupViewModel;

namespace {

QString databaseIn(const QTemporaryDir& dir) { return dir.filePath(QStringLiteral("club.sqlite")); }

} // namespace

// P19 review: a lock file that cannot be written stops the start, as a running instance does.
TEST(StartupViewModelTest, AReadOnlyDataFolderStopsTheStart) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    ASSERT_TRUE(QFile::setPermissions(dir.path(), QFile::ReadOwner | QFile::ExeOwner));
    QFile probe(dir.filePath(QStringLiteral("probe")));
    if (probe.open(QIODevice::WriteOnly)) {
        probe.close();
        QFile::setPermissions(dir.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        GTEST_SKIP() << "this user writes into read-only folders (root?)";
    }

    const auto lock = StartupViewModel::lockInstance(databaseIn(dir));
    QFile::setPermissions(dir.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

    ASSERT_FALSE(lock.has_value());
    EXPECT_EQ(lock.error().code, ErrorCode::Io);
    StartupViewModel startup;
    startup.setInstanceError(lock.error(), databaseIn(dir));
    EXPECT_FALSE(startup.otherInstanceRunning());
    EXPECT_EQ(startup.databasePath(), databaseIn(dir));
}

TEST(StartupViewModelTest, ASecondStartIsRefusedAsAnotherInstance) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const auto first = StartupViewModel::lockInstance(databaseIn(dir));
    ASSERT_TRUE(first.has_value());
    ASSERT_NE(*first, nullptr);

    const auto second = StartupViewModel::lockInstance(databaseIn(dir));

    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, ErrorCode::Conflict);
    StartupViewModel startup;
    startup.setInstanceError(Error{ErrorCode::Io, {}, {}}, {});
    startup.setInstanceError(second.error(), databaseIn(dir));
    EXPECT_TRUE(startup.otherInstanceRunning());
}

TEST(StartupViewModelTest, WithoutADataFolderTheDatabaseErrorDecides) {
    const fdss::core::Result<QString> noFolder =
        std::unexpected(Error{ErrorCode::Io, "no folder", {}});
    const auto lock = StartupViewModel::lockInstance(noFolder);
    ASSERT_TRUE(lock.has_value());
    EXPECT_EQ(*lock, nullptr);
}
