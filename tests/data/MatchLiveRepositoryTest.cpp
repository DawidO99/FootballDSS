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
constexpr ClockStep kStart1{1, ClockStepKind::Start};
constexpr ClockStep kEnd1{1, ClockStepKind::End};

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

TEST_F(MatchLiveRepositoryTest, FormatAndStepsComeBackInOrder) {
    auto live = repository();
    ASSERT_TRUE(live.create(kMatch, {3, 30}));
    ASSERT_TRUE(live.appendStep(kMatch, kStart1, kKickoff, 1));
    ASSERT_TRUE(live.appendStep(kMatch, kEnd1, kKickoff + 32min, 4));
    ASSERT_TRUE(live.appendStep(kMatch, {2, ClockStepKind::Start}, kKickoff + 40min, 5));
    const auto found = live.find(kMatch);
    ASSERT_TRUE(found.has_value()) << found.error().detail;
    ASSERT_TRUE(found->has_value());
    EXPECT_EQ((*found)->format, (MatchFormat{3, 30}));
    EXPECT_EQ((*found)->finishedSeq, std::nullopt);
    EXPECT_EQ((*found)->steps,
              (std::vector<ClockStepRow>{{kStart1, kKickoff, 1},
                                         {kEnd1, kKickoff + 32min, 4},
                                         {{2, ClockStepKind::Start}, kKickoff + 40min, 5}}));
}

TEST_F(MatchLiveRepositoryTest, StepsOfOneActionGoTogether) {
    auto live = repository();
    ASSERT_TRUE(live.create(kMatch, {2, 45}));
    ASSERT_TRUE(live.appendStep(kMatch, kStart1, kKickoff, 1));
    ASSERT_TRUE(live.appendStep(kMatch, kEnd1, kKickoff + 47min, 2));
    const auto deleted = live.deleteSteps(kMatch, 2);
    ASSERT_TRUE(deleted.has_value()) << deleted.error().detail;
    EXPECT_EQ(*deleted, (std::vector<ClockStepRow>{{kEnd1, kKickoff + 47min, 2}}));
    EXPECT_TRUE(live.deleteSteps(kMatch, 9)->empty());
    EXPECT_EQ(live.find(kMatch).value()->steps.size(), 1U);
}

TEST_F(MatchLiveRepositoryTest, SeqIsACounterThatOnlyGrows) {
    auto live = repository();
    ASSERT_TRUE(live.create(kMatch, {2, 45}));
    EXPECT_EQ(live.takeSeq(kMatch).value(), 1);
    EXPECT_EQ(live.takeSeq(kMatch).value(), 2);
    // Rows of that seq going away does not give it back.
    EXPECT_EQ(live.takeSeq(kMatch).value(), 3);
    EXPECT_EQ(live.takeSeq(MatchId{999}).error().code, ErrorCode::NotFound);
}

TEST_F(MatchLiveRepositoryTest, FinishedWithTheMinuteOfAnEarlyEnd) {
    auto live = repository();
    ASSERT_TRUE(live.create(kMatch, {2, 45}));
    ASSERT_TRUE(live.setFinished(kMatch, 4, 60));
    EXPECT_EQ(live.find(kMatch).value()->finishedSeq, 4);
    EXPECT_EQ(live.find(kMatch).value()->endedEarlyAt, 60);
    ASSERT_TRUE(live.setFinished(kMatch, std::nullopt, 60)); // live again: no early end
    EXPECT_EQ(live.find(kMatch).value()->finishedSeq, std::nullopt);
    EXPECT_EQ(live.find(kMatch).value()->endedEarlyAt, std::nullopt);
    ASSERT_TRUE(live.setFinished(kMatch, 5));
    EXPECT_EQ(live.find(kMatch).value()->endedEarlyAt, std::nullopt);
}

TEST_F(MatchLiveRepositoryTest, ConstraintsMapToErrors) {
    auto live = repository();
    const auto badFormat = live.create(kMatch, {4, 40});
    ASSERT_FALSE(badFormat.has_value());
    EXPECT_EQ(badFormat.error().code, ErrorCode::InvalidInput);
    const auto missingMatch = live.create(MatchId{999}, {2, 45});
    ASSERT_FALSE(missingMatch.has_value());
    EXPECT_EQ(missingMatch.error().code, ErrorCode::InvalidInput);
    const auto noRow = live.appendStep(kMatch, kStart1, kKickoff, 1);
    ASSERT_FALSE(noRow.has_value());
    EXPECT_EQ(noRow.error().code, ErrorCode::InvalidInput);

    ASSERT_TRUE(live.create(kMatch, {2, 45}));
    const auto twice = live.create(kMatch, {2, 45});
    ASSERT_FALSE(twice.has_value());
    EXPECT_EQ(twice.error().code, ErrorCode::Conflict);
    ASSERT_TRUE(live.appendStep(kMatch, kStart1, kKickoff, 1));
    const auto stepTwice = live.appendStep(kMatch, kStart1, kKickoff + 1min, 2);
    ASSERT_FALSE(stepTwice.has_value());
    EXPECT_EQ(stepTwice.error().code, ErrorCode::Conflict);
}

TEST_F(MatchLiveRepositoryTest, RemoveTakesTheStepsAlong) {
    auto live = repository();
    ASSERT_TRUE(live.create(kMatch, {2, 45}));
    ASSERT_TRUE(live.appendStep(kMatch, kStart1, kKickoff, 1));
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
        ASSERT_TRUE(repository().create(kMatch, {2, 45}));
        // Rolled back by the destructor.
    }
    const auto found = repository().find(kMatch);
    ASSERT_TRUE(found.has_value());
    EXPECT_FALSE(found->has_value());
}
