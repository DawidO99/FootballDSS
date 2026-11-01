// Own gtest main: QtSql and QStandardPaths need a QCoreApplication, and tests never touch the real
// AppData folder.

#include <QCoreApplication>
#include <QStandardPaths>

#include <gtest/gtest.h>

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("FootballDSS"));
    QCoreApplication::setApplicationName(QStringLiteral("fdss_tests"));
    QStandardPaths::setTestModeEnabled(true);
    return RUN_ALL_TESTS();
}
