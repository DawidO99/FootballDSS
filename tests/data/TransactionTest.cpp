#include "data/db/Transaction.hpp"

#include "TestDatabase.hpp"
#include "data/db/DatabaseManager.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <utility>

using namespace fdss::data;
using namespace fdss::tests;
using fdss::core::ErrorCode;

namespace {

class TransactionTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto opened = DatabaseManager::openInMemory();
        ASSERT_TRUE(opened.has_value()) << opened.error().detail;
        m_db = std::move(*opened);
        ASSERT_TRUE(run(m_db->database(), QStringLiteral("CREATE TABLE t (v INTEGER)")));
    }

    [[nodiscard]] ::testing::AssertionResult insert(int value) const {
        return run(m_db->database(), QStringLiteral("INSERT INTO t (v) VALUES (?)"), {value});
    }

    // Stored values in insertion order, e.g. "1,3". Empty when the table is empty.
    [[nodiscard]] QString rows() const {
        return scalar(
                   m_db->database(),
                   QStringLiteral("SELECT group_concat(v) FROM (SELECT v FROM t ORDER BY rowid)"))
            .toString();
    }

    std::unique_ptr<DatabaseManager> m_db;
};

class TransactionFileTest : public TempDatabaseTest {};

} // namespace

TEST_F(TransactionTest, CommitKeepsTheWork) {
    auto transaction = Transaction::begin(*m_db);
    ASSERT_TRUE(transaction.has_value()) << transaction.error().detail;
    ASSERT_TRUE(insert(1));
    ASSERT_TRUE(transaction->commit().has_value());
    EXPECT_EQ(rows(), QStringLiteral("1"));
}

TEST_F(TransactionTest, DestructorWithoutCommitRollsBack) {
    {
        auto transaction = Transaction::begin(*m_db);
        ASSERT_TRUE(transaction.has_value()) << transaction.error().detail;
        ASSERT_TRUE(insert(1));
    }
    EXPECT_EQ(rows(), QString{});
}

TEST_F(TransactionTest, InnerAndOuterCommitKeepEverything) {
    auto outer = Transaction::begin(*m_db);
    ASSERT_TRUE(outer.has_value()) << outer.error().detail;
    ASSERT_TRUE(insert(1));
    {
        auto inner = Transaction::begin(*m_db);
        ASSERT_TRUE(inner.has_value()) << inner.error().detail;
        ASSERT_TRUE(insert(2));
        ASSERT_TRUE(inner->commit().has_value());
    }
    ASSERT_TRUE(insert(3));
    ASSERT_TRUE(outer->commit().has_value());
    EXPECT_EQ(rows(), QStringLiteral("1,2,3"));
}

TEST_F(TransactionTest, InnerRollbackKeepsOuterWork) {
    auto outer = Transaction::begin(*m_db);
    ASSERT_TRUE(outer.has_value()) << outer.error().detail;
    ASSERT_TRUE(insert(1));
    {
        auto inner = Transaction::begin(*m_db);
        ASSERT_TRUE(inner.has_value()) << inner.error().detail;
        ASSERT_TRUE(insert(2));
    }
    ASSERT_TRUE(insert(3));
    ASSERT_TRUE(outer->commit().has_value());
    EXPECT_EQ(rows(), QStringLiteral("1,3"));
}

TEST_F(TransactionTest, OuterRollbackDropsEverythingEvenCommittedInnerWork) {
    {
        auto outer = Transaction::begin(*m_db);
        ASSERT_TRUE(outer.has_value()) << outer.error().detail;
        ASSERT_TRUE(insert(1));
        {
            auto inner = Transaction::begin(*m_db);
            ASSERT_TRUE(inner.has_value()) << inner.error().detail;
            ASSERT_TRUE(insert(2));
            ASSERT_TRUE(inner->commit().has_value());
        }
    }
    EXPECT_EQ(rows(), QString{});
}

TEST_F(TransactionTest, ThreeLevelsRollBackOnlyTheMiddleBranch) {
    auto outer = Transaction::begin(*m_db);
    ASSERT_TRUE(outer.has_value()) << outer.error().detail;
    ASSERT_TRUE(insert(1));
    {
        auto middle = Transaction::begin(*m_db);
        ASSERT_TRUE(middle.has_value()) << middle.error().detail;
        ASSERT_TRUE(insert(2));
        {
            auto innermost = Transaction::begin(*m_db);
            ASSERT_TRUE(innermost.has_value()) << innermost.error().detail;
            ASSERT_TRUE(insert(3));
            ASSERT_TRUE(innermost->commit().has_value());
        }
    }
    ASSERT_TRUE(outer->commit().has_value());
    EXPECT_EQ(rows(), QStringLiteral("1"));
}

TEST_F(TransactionTest, SiblingInnerTransactionsAreIndependent) {
    auto outer = Transaction::begin(*m_db);
    ASSERT_TRUE(outer.has_value()) << outer.error().detail;
    {
        auto rolledBack = Transaction::begin(*m_db);
        ASSERT_TRUE(rolledBack.has_value()) << rolledBack.error().detail;
        ASSERT_TRUE(insert(1));
    }
    {
        auto committed = Transaction::begin(*m_db);
        ASSERT_TRUE(committed.has_value()) << committed.error().detail;
        ASSERT_TRUE(insert(2));
        ASSERT_TRUE(committed->commit().has_value());
    }
    ASSERT_TRUE(outer->commit().has_value());
    EXPECT_EQ(rows(), QStringLiteral("2"));
}

TEST_F(TransactionTest, NewTransactionWorksAfterEitherEnding) {
    {
        auto rolledBack = Transaction::begin(*m_db);
        ASSERT_TRUE(rolledBack.has_value()) << rolledBack.error().detail;
    }
    {
        auto committed = Transaction::begin(*m_db);
        ASSERT_TRUE(committed.has_value()) << committed.error().detail;
        ASSERT_TRUE(committed->commit().has_value());
    }
    auto next = Transaction::begin(*m_db);
    ASSERT_TRUE(next.has_value()) << next.error().detail;
    ASSERT_TRUE(insert(1));
    ASSERT_TRUE(next->commit().has_value());
    EXPECT_EQ(rows(), QStringLiteral("1"));
}

TEST_F(TransactionTest, SecondCommitIsInternal) {
    auto transaction = Transaction::begin(*m_db);
    ASSERT_TRUE(transaction.has_value()) << transaction.error().detail;
    ASSERT_TRUE(transaction->commit().has_value());
    const auto again = transaction->commit();
    ASSERT_FALSE(again.has_value());
    EXPECT_EQ(again.error().code, ErrorCode::Internal);
}

TEST_F(TransactionTest, CommittingOuterWhileInnerIsOpenIsInternal) {
    {
        auto outer = Transaction::begin(*m_db);
        ASSERT_TRUE(outer.has_value()) << outer.error().detail;
        ASSERT_TRUE(insert(1));
        auto inner = Transaction::begin(*m_db);
        ASSERT_TRUE(inner.has_value()) << inner.error().detail;
        const auto committed = outer->commit();
        ASSERT_FALSE(committed.has_value());
        EXPECT_EQ(committed.error().code, ErrorCode::Internal);
    }
    EXPECT_EQ(rows(), QString{});
}

TEST_F(TransactionTest, MovedTransactionFinishesExactlyOnce) {
    auto original = Transaction::begin(*m_db);
    ASSERT_TRUE(original.has_value()) << original.error().detail;
    ASSERT_TRUE(insert(1));
    {
        Transaction moved(std::move(*original));
        const auto fromOld = original->commit();
        ASSERT_FALSE(fromOld.has_value());
        EXPECT_EQ(fromOld.error().code, ErrorCode::Internal);
        ASSERT_TRUE(moved.commit().has_value());
    }
    EXPECT_EQ(rows(), QStringLiteral("1"));
}

TEST_F(TransactionTest, ForeignKeyErrorInsideLeavesTheTransactionUsable) {
    auto transaction = Transaction::begin(*m_db);
    ASSERT_TRUE(transaction.has_value()) << transaction.error().detail;
    ASSERT_TRUE(insert(1));
    EXPECT_FALSE(
        run(m_db->database(), QStringLiteral("INSERT INTO team (club_id, name, category, season) "
                                             "VALUES (999, 'X', 'senior', '2026/27')")));
    ASSERT_TRUE(transaction->commit().has_value());
    EXPECT_EQ(rows(), QStringLiteral("1"));
}

TEST_F(TransactionFileTest, BeginReportsAHeldWriteLockAndLeavesNoState) {
    const auto opened = DatabaseManager::open(databasePath(), options());
    ASSERT_TRUE(opened.has_value()) << opened.error().detail;
    DatabaseManager& db = **opened;

    const RawConnection other(databasePath());
    ASSERT_TRUE(run(other.database(), QStringLiteral("BEGIN IMMEDIATE")));
    {
        const auto blocked = Transaction::begin(db);
        ASSERT_FALSE(blocked.has_value());
        EXPECT_EQ(blocked.error().code, ErrorCode::DatabaseLocked) << blocked.error().detail;
    }
    ASSERT_TRUE(run(other.database(), QStringLiteral("ROLLBACK")));

    // A failed begin must not count as an open transaction: this one is outermost again.
    auto next = Transaction::begin(db);
    ASSERT_TRUE(next.has_value()) << next.error().detail;
    ASSERT_TRUE(run(db.database(), QStringLiteral("INSERT INTO club (name) VALUES ('KS Test')")));
    ASSERT_TRUE(next->commit().has_value());
    EXPECT_EQ(scalar(other.database(), QStringLiteral("SELECT count(*) FROM club")).toInt(), 1);
}

TEST_F(TransactionFileTest, CommittedWorkSurvivesReopen) {
    {
        const auto opened = DatabaseManager::open(databasePath(), options());
        ASSERT_TRUE(opened.has_value()) << opened.error().detail;
        auto transaction = Transaction::begin(**opened);
        ASSERT_TRUE(transaction.has_value()) << transaction.error().detail;
        ASSERT_TRUE(run((*opened)->database(),
                        QStringLiteral("INSERT INTO club (name) VALUES ('KS Test')")));
        ASSERT_TRUE(transaction->commit().has_value());
    }
    const auto reopened = DatabaseManager::open(databasePath(), options());
    ASSERT_TRUE(reopened.has_value()) << reopened.error().detail;
    EXPECT_EQ(scalar((*reopened)->database(), QStringLiteral("SELECT count(*) FROM club")).toInt(),
              1);
}
