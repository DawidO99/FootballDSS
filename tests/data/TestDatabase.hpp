#pragma once

// Helpers shared by the data tests.

#include "data/db/DatabaseManager.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariant>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace fdss::tests {

// Runs one prepared statement; the failure message carries the SQLite error.
inline ::testing::AssertionResult run(const QSqlDatabase& db, const QString& sql,
                                      const QVariantList& values = {}) {
    QSqlQuery query(db);
    if (!query.prepare(sql)) {
        return ::testing::AssertionFailure()
               << "prepare failed: " << query.lastError().text().toStdString();
    }
    for (qsizetype i = 0; i < values.size(); ++i) {
        query.bindValue(static_cast<int>(i), values.at(i));
    }
    if (!query.exec()) {
        return ::testing::AssertionFailure()
               << "exec failed: " << query.lastError().text().toStdString();
    }
    return ::testing::AssertionSuccess();
}

// First column of the first row; invalid when the query fails or returns nothing.
inline QVariant scalar(const QSqlDatabase& db, const QString& sql,
                       const QVariantList& values = {}) {
    QSqlQuery query(db);
    if (!query.prepare(sql)) {
        return {};
    }
    for (qsizetype i = 0; i < values.size(); ++i) {
        query.bindValue(static_cast<int>(i), values.at(i));
    }
    if (!query.exec() || !query.next()) {
        return {};
    }
    return query.value(0);
}

// A second connection to a database file: plays another process, or looks at the file behind the
// back of the DatabaseManager.
class RawConnection {
public:
    explicit RawConnection(const QString& filePath)
        : m_name(QStringLiteral("raw_%1").arg(reinterpret_cast<quintptr>(this), 0, 16)) {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_name);
        db.setDatabaseName(filePath);
        db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=0"));
        db.open();
    }
    ~RawConnection() {
        {
            QSqlDatabase db = database();
            db.close();
        }
        QSqlDatabase::removeDatabase(m_name);
    }
    RawConnection(const RawConnection&) = delete;
    RawConnection& operator=(const RawConnection&) = delete;

    [[nodiscard]] QSqlDatabase database() const { return QSqlDatabase::database(m_name, false); }

private:
    QString m_name;
};

inline QByteArray readFile(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

inline bool writeFile(const QString& path, const QByteArray& content) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}

inline QStringList filesIn(const QString& directory) {
    return QDir(directory).entryList(QDir::Files, QDir::Name);
}

// A database folder of its own for every test, so nothing depends on earlier runs.
class TempDatabaseTest : public ::testing::Test {
protected:
    void SetUp() override { ASSERT_TRUE(m_directory.isValid()); }

    [[nodiscard]] QString databasePath() const {
        return m_directory.filePath(QStringLiteral("club.sqlite"));
    }
    [[nodiscard]] QString backupDirectory() const {
        return m_directory.filePath(QStringLiteral("backups"));
    }
    // Short timeout: the tests that hit a lock would otherwise wait for the 5 s default.
    [[nodiscard]] data::OpenOptions options() const {
        data::OpenOptions result;
        result.busyTimeoutMs = 100;
        result.backupDirectory = backupDirectory();
        return result;
    }
    [[nodiscard]] data::OpenOptions options(std::vector<data::Migration> migrations) const {
        data::OpenOptions result = options();
        result.migrations = std::move(migrations);
        return result;
    }

    QTemporaryDir m_directory;
};

// Small migrations for testing the runner without depending on the real schema.
inline data::Migration firstMigration() {
    return {1, QStringLiteral("first"),
            QStringLiteral("CREATE TABLE schema_version (\n"
                           "    id INTEGER PRIMARY KEY CHECK (id = 1),\n"
                           "    version INTEGER NOT NULL\n"
                           ");\n"
                           "CREATE TABLE a (id INTEGER PRIMARY KEY);\n")};
}

inline data::Migration secondMigration() {
    return {2, QStringLiteral("second"),
            QStringLiteral("CREATE TABLE b (id INTEGER PRIMARY KEY);\n")};
}

// The first statement works, so the test can check that it is rolled back with the second.
inline data::Migration failingMigration() {
    return {2, QStringLiteral("failing"),
            QStringLiteral("CREATE TABLE b (id INTEGER PRIMARY KEY);\n"
                           "THIS IS NOT SQL;\n")};
}

inline bool tableExists(const QSqlDatabase& db, const QString& name) {
    return scalar(db, QStringLiteral("SELECT count(*) FROM sqlite_master WHERE name = ?"), {name})
               .toInt() == 1;
}

} // namespace fdss::tests
