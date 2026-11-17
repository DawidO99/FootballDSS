#include "data/repositories/MatchLiveRepository.hpp"

#include "RepositoryFixture.hpp"
#include "data/db/Transaction.hpp"

#include <gtest/gtest.h>

#include <chrono>

using namespace fdss::core;
using namespace fdss::data;
using namespace fdss::tests;
using namespace std::chrono_literals;

namespace {

const Timestamp kKickoff = std::chrono::sys_days{std::chrono::year{2026} / 11 / 1} + 14h;

class MatchLiveRepositoryTest : public RepositoryTest {
protected:
    void SetUp() override {
        RepositoryTest::SetUp();
        seedCalendar();
    }

    [[nodiscard]] MatchLiveRepository repository() { return MatchLiveRepository(*m_db); }
};

} // namespace

TEST_F(MatchLiveRepositoryTest, NoLiveRowIsNullopt) {
    const auto found = repository().find(kMatch);
    ASSERT_TRUE(found.has_value()) << found.error().detail;
    EXPECT_FALSE(found->has_value());
}

TEST_F(MatchLiveRepositoryTest, StepsComeBackInOrderWithTheirTimes) {
    auto live = repository();
    ASSERT_TRUE(live.create(kMatch, 40));
    ASSERT_TRUE(live.appendStep(kMatch, ClockStep::FirstHalfStart, kKickoff, std::nullopt));
    ASSERT_TRUE(
        live.appendStep(kMatch, ClockStep::FirstHalfEnd, kKickoff + 42min, MatchEventId{7}));
    const auto found = live.find(kMatch);
    ASSERT_TRUE(found.has_value()) << found.error().detail;
    ASSERT_TRUE(found->has_value());
    EXPECT_EQ((*found)->halfLength, 40);
    EXPECT_EQ((*found)->steps, (std::vector<ClockStepRow>{
                                   {ClockStep::FirstHalfStart, kKickoff, std::nullopt},
                                   {ClockStep::FirstHalfEnd, kKickoff + 42min, MatchEventId{7}}}));
}

TEST_F(MatchLiveRepositoryTest, DeleteLastStepReturnsIt) {
    auto live = repository();
    ASSERT_TRUE(live.create(kMatch, 45));
    ASSERT_TRUE(live.appendStep(kMatch, ClockStep::FirstHalfStart, kKickoff, std::nullopt));
    ASSERT_TRUE(live.appendStep(kMatch, ClockStep::FirstHalfEnd, kKickoff + 47min, std::nullopt));
    const auto deleted = live.deleteLastStep(kMatch);
    ASSERT_TRUE(deleted.has_value()) << deleted.error().detail;
    EXPECT_EQ(deleted->step, ClockStep::FirstHalfEnd);
    EXPECT_EQ(deleted->at, kKickoff + 47min);
    ASSERT_TRUE(live.deleteLastStep(kMatch));
    const auto none = live.deleteLastStep(kMatch);
    ASSERT_FALSE(none.has_value());
    EXPECT_EQ(none.error().code, ErrorCode::NotFound);
}

TEST_F(MatchLiveRepositoryTest, ConstraintsMapToErrors) {
    auto live = repository();
    const auto badLength = live.create(kMatch, 5);
    ASSERT_FALSE(badLength.has_value());
    EXPECT_EQ(badLength.error().code, ErrorCode::InvalidInput);
    const auto missingMatch = live.create(MatchId{999}, 45);
    ASSERT_FALSE(missingMatch.has_value());
    EXPECT_EQ(missingMatch.error().code, ErrorCode::InvalidInput);
    const auto noRow = live.appendStep(kMatch, ClockStep::FirstHalfStart, kKickoff, std::nullopt);
    ASSERT_FALSE(noRow.has_value());
    EXPECT_EQ(noRow.error().code, ErrorCode::InvalidInput);

    ASSERT_TRUE(live.create(kMatch, 45));
    const auto twice = live.create(kMatch, 45);
    ASSERT_FALSE(twice.has_value());
    EXPECT_EQ(twice.error().code, ErrorCode::Conflict);
    ASSERT_TRUE(live.appendStep(kMatch, ClockStep::FirstHalfStart, kKickoff, std::nullopt));
    const auto stepTwice =
        live.appendStep(kMatch, ClockStep::FirstHalfStart, kKickoff + 1min, std::nullopt);
    ASSERT_FALSE(stepTwice.has_value());
    EXPECT_EQ(stepTwice.error().code, ErrorCode::Conflict);
}

TEST_F(MatchLiveRepositoryTest, RemoveTakesTheStepsAlong) {
    auto live = repository();
    ASSERT_TRUE(live.create(kMatch, 45));
    ASSERT_TRUE(live.appendStep(kMatch, ClockStep::FirstHalfStart, kKickoff, std::nullopt));
    ASSERT_TRUE(live.remove(kMatch));
    EXPECT_EQ(count("SELECT count(*) FROM match_clock_step"), 0);
    const auto again = live.remove(kMatch);
    ASSERT_FALSE(again.has_value());
    EXPECT_EQ(again.error().code, ErrorCode::NotFound);
}

TEST_F(MatchLiveRepositoryTest, WritesJoinTheTransactionOfTheCaller) {
    {
        auto transaction = Transaction::begin(*m_db);
        ASSERT_TRUE(transaction.has_value());
        ASSERT_TRUE(repository().create(kMatch, 45));
        // Rolled back by the destructor.
    }
    const auto found = repository().find(kMatch);
    ASSERT_TRUE(found.has_value());
    EXPECT_FALSE(found->has_value());
}
