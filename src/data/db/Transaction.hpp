#pragma once

#include "core/util/Error.hpp"

namespace fdss::data {

class DatabaseManager;

// RAII and nestable, so a service can wrap several repository calls atomically. The outermost
// Transaction runs BEGIN IMMEDIATE / COMMIT, inner ones run SAVEPOINT / RELEASE. The destructor
// rolls back unless commit() succeeded (ROLLBACK, or ROLLBACK TO + RELEASE for an inner one).
//
// begin() instead of a constructor: a constructor cannot report a failed BEGIN, and the statements
// after it would then commit one by one.
class [[nodiscard]] Transaction {
public:
    [[nodiscard]] static core::Result<Transaction> begin(DatabaseManager& db);

    Transaction(Transaction&& other) noexcept;
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    Transaction& operator=(Transaction&&) = delete;
    ~Transaction();

    // A failed commit leaves the transaction open; the destructor then rolls it back.
    core::Status commit();

private:
    Transaction(DatabaseManager& db, int depth) noexcept;

    DatabaseManager* m_db; // nullptr once finished or moved from
    int m_depth;
};

} // namespace fdss::data
