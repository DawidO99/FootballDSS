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
    // On a lost transaction a SAVEPOINT would start a new one, which its RELEASE would then commit.
    if (db.m_transactionBroken) {
        return std::unexpected(DatabaseManager::brokenTransactionError());
    }
    const int depth = db.m_transactionDepth + 1;
    const QString sql = depth == 1 ? QStringLiteral("BEGIN IMMEDIATE") : kSavepoint;
    if (auto status = db.runStatement(sql, "begin transaction"); !status) {
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
    // Failures are logged: a destructor has no caller to report to.
    if (m_depth == 1) {
        // On a broken stack SQLite may have rolled back already, and this then fails harmlessly.
        logIfFailed(m_db->runStatement(QStringLiteral("ROLLBACK"), "roll back"));
        m_db->m_transactionBroken = false;
    } else if (!m_db->m_transactionBroken) {
        const core::Status rolledBack =
            m_db->runStatement(kRollbackToSavepoint, "roll back to savepoint");
        const core::Status released = m_db->runStatement(kReleaseSavepoint, "release savepoint");
        logIfFailed(rolledBack);
        logIfFailed(released);
        // The savepoint is gone, so the transaction was lost in a way sqlError() did not see.
        if (!rolledBack || !released) {
            m_db->m_transactionBroken = true;
        }
    }
    m_db->m_transactionDepth = m_depth - 1;
}

core::Status Transaction::commit() {
    if (m_db == nullptr) {
        return std::unexpected(
            core::Error{core::ErrorCode::Internal, "transaction is already finished", {}});
    }
    if (m_db->m_transactionBroken) {
        return std::unexpected(DatabaseManager::brokenTransactionError());
    }
    if (m_db->m_transactionDepth != m_depth) {
        return std::unexpected(
            core::Error{core::ErrorCode::Internal, "an inner transaction is still open", {}});
    }
    const QString sql = m_depth == 1 ? QStringLiteral("COMMIT") : kReleaseSavepoint;
    if (auto status = m_db->runStatement(sql, "commit transaction"); !status) {
        // A savepoint that cannot be released is gone: see the destructor.
        if (m_depth > 1) {
            m_db->m_transactionBroken = true;
        }
        return status;
    }
    m_db->m_transactionDepth = m_depth - 1;
    m_db = nullptr;
    return {};
}

} // namespace fdss::data
