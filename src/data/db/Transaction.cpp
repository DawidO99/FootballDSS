#include "data/db/Transaction.hpp"

#include "data/db/DatabaseManager.hpp"

#include <utility>

namespace fdss::data {

namespace {

// One constant name: SQLite savepoints nest like a stack, and ROLLBACK TO / RELEASE act on the most
// recent one with that name. So no SQL has to be built from the depth.
const auto kSavepoint = QStringLiteral("SAVEPOINT fdss_sp");
const auto kRollbackToSavepoint = QStringLiteral("ROLLBACK TO fdss_sp");
const auto kReleaseSavepoint = QStringLiteral("RELEASE fdss_sp");

void logIfFailed(const core::Status& status) {
    if (!status) {
        qWarning("Transaction: %s", status.error().detail.c_str());
    }
}

} // namespace

core::Result<Transaction> Transaction::begin(DatabaseManager& db) {
    const int depth = db.m_transactionDepth + 1;
    const QString sql = depth == 1 ? QStringLiteral("BEGIN IMMEDIATE") : kSavepoint;
    if (auto status = execSql(db.database(), sql, "begin transaction"); !status) {
        return std::unexpected(status.error());
    }
    db.m_transactionDepth = depth;
    return Transaction(db, depth);
}

Transaction::Transaction(DatabaseManager& db, int depth) noexcept : m_db(&db), m_depth(depth) {}

Transaction::Transaction(Transaction&& other) noexcept
    : m_db(std::exchange(other.m_db, nullptr)), m_depth(other.m_depth) {}

Transaction::~Transaction() {
    if (m_db == nullptr) {
        return;
    }
    Q_ASSERT(m_db->m_transactionDepth == m_depth);
    const QSqlDatabase connection = m_db->database();
    // Failures are logged: a destructor has no caller to report to, and SQLite may already have
    // rolled back by itself (disk full, for example).
    if (m_depth == 1) {
        logIfFailed(execSql(connection, QStringLiteral("ROLLBACK"), "roll back"));
    } else {
        logIfFailed(execSql(connection, kRollbackToSavepoint, "roll back to savepoint"));
        logIfFailed(execSql(connection, kReleaseSavepoint, "release savepoint"));
    }
    m_db->m_transactionDepth = m_depth - 1;
}

core::Status Transaction::commit() {
    if (m_db == nullptr) {
        return std::unexpected(
            core::Error{core::ErrorCode::Internal, "transaction is already finished", {}});
    }
    if (m_db->m_transactionDepth != m_depth) {
        return std::unexpected(
            core::Error{core::ErrorCode::Internal, "an inner transaction is still open", {}});
    }
    const QString sql = m_depth == 1 ? QStringLiteral("COMMIT") : kReleaseSavepoint;
    if (auto status = execSql(m_db->database(), sql, "commit transaction"); !status) {
        return status;
    }
    m_db->m_transactionDepth = m_depth - 1;
    m_db = nullptr;
    return {};
}

} // namespace fdss::data
