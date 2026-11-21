#include "data/repositories/MatchTacticRepository.hpp"

#include "RepositoryFixture.hpp"
#include "core/algorithms/Formations.hpp"

#include <gtest/gtest.h>

using namespace fdss::core;
using namespace fdss::data;
using namespace fdss::tests;

namespace {

class MatchTacticRepositoryTest : public RepositoryTest {
protected:
    void SetUp() override {
        RepositoryTest::SetUp();
        seedCalendar();
    }

    [[nodiscard]] MatchTacticRepository repository() { return MatchTacticRepository(*m_db); }

    // The places of a formation, with a player in the first slot (it is not stored).
    static MatchTacticChangeDraft change(int minute, std::optional<int> added, const char* name,
                                         const char* storedName) {
        Lineup shape = emptyLineup(*findFormation(name));
        shape.pitchSlots.front().player = PlayerId{1};
        shape.bench = {PlayerId{2}};
        shape.formation = storedName;
        return {minute, added, shape};
    }
};

} // namespace

TEST_F(MatchTacticRepositoryTest, ChangesKeepTheirPlacesWithoutPlayersInTheOrderOfEntry) {
    auto tactics = repository();
    const auto later = tactics.create(kMatch, change(60, std::nullopt, "4-2-3-1", "4-2-3-1"), 7);
    ASSERT_TRUE(later.has_value()) << later.error().detail;
    const auto earlier = tactics.create(kMatch, change(45, 2, "4-3-3", "4-1-2-2-1"), 3);
    ASSERT_TRUE(earlier.has_value()) << earlier.error().detail;

    const auto list = tactics.list(kMatch);
    ASSERT_TRUE(list.has_value()) << list.error().detail;
    ASSERT_EQ(list->size(), 2U);
    const MatchTacticChange& first = list->at(0);
    EXPECT_EQ(first.id, *earlier);
    EXPECT_EQ(first.matchId, kMatch);
    EXPECT_EQ(first.minute, 45);
    EXPECT_EQ(first.addedTime, 2);
    EXPECT_EQ(first.seq, 3);
    // The stored name of the column, not the one counted from the 11 slots of the file.
    EXPECT_EQ(first.shape.formation, "4-1-2-2-1");
    const Lineup expected = emptyLineup(*findFormation("4-3-3"));
    ASSERT_EQ(first.shape.pitchSlots.size(), expected.pitchSlots.size());
    for (std::size_t i = 0; i < expected.pitchSlots.size(); ++i) {
        EXPECT_EQ(first.shape.pitchSlots[i].position, expected.pitchSlots[i].position);
        EXPECT_DOUBLE_EQ(first.shape.pitchSlots[i].x, expected.pitchSlots[i].x);
        EXPECT_DOUBLE_EQ(first.shape.pitchSlots[i].y, expected.pitchSlots[i].y);
        EXPECT_FALSE(first.shape.pitchSlots[i].player);
    }
    EXPECT_TRUE(first.shape.bench.empty());
    EXPECT_EQ(list->at(1).id, *later);
    EXPECT_EQ(count("SELECT count(*) FROM match_tactic_change WHERE tactic_json LIKE "
                    "'%\"playerId\":1%'"),
              0);
}

TEST_F(MatchTacticRepositoryTest, DeleteWithSeqTakesOnlyThatAction) {
    auto tactics = repository();
    ASSERT_TRUE(tactics.create(kMatch, change(10, std::nullopt, "4-4-2", "4-4-2"), 1));
    ASSERT_TRUE(tactics.create(kMatch, change(20, std::nullopt, "4-3-3", "4-3-3"), 2));
    const auto deleted = tactics.deleteWithSeq(kMatch, 2);
    ASSERT_TRUE(deleted.has_value());
    ASSERT_TRUE(deleted->has_value());
    EXPECT_EQ((*deleted)->minute, 20);
    EXPECT_EQ((*deleted)->shape.formation, "4-3-3");
    EXPECT_EQ(tactics.list(kMatch)->size(), 1U);
    const auto none = tactics.deleteWithSeq(kMatch, 2);
    ASSERT_TRUE(none.has_value());
    EXPECT_FALSE(none->has_value());
}

TEST_F(MatchTacticRepositoryTest, InvalidRowsAreRefused) {
    auto tactics = repository();
    EXPECT_EQ(
        tactics.create(MatchId{999}, change(10, std::nullopt, "4-4-2", "4-4-2"), 1).error().code,
        ErrorCode::InvalidInput);
    EXPECT_EQ(tactics.create(kMatch, change(131, std::nullopt, "4-4-2", "4-4-2"), 1).error().code,
              ErrorCode::InvalidInput);
    EXPECT_EQ(tactics.create(kMatch, change(10, 0, "4-4-2", "4-4-2"), 1).error().code,
              ErrorCode::InvalidInput);
    EXPECT_EQ(tactics.create(kMatch, change(10, std::nullopt, "4-4-2", ""), 1).error().code,
              ErrorCode::InvalidInput);
    ASSERT_TRUE(tactics.create(kMatch, change(10, std::nullopt, "4-4-2", "4-4-2"), 1));
    EXPECT_EQ(tactics.create(kMatch, change(11, std::nullopt, "4-3-3", "4-3-3"), 1).error().code,
              ErrorCode::Conflict);
}

TEST_F(MatchTacticRepositoryTest, ADamagedStoredShapeIsADatabaseError) {
    auto tactics = repository();
    ASSERT_TRUE(tactics.create(kMatch, change(10, std::nullopt, "4-4-2", "4-4-2"), 1));
    ASSERT_TRUE(sql("UPDATE match_tactic_change SET tactic_json = '{\"version\": 1}'"));
    EXPECT_EQ(tactics.list(kMatch).error().code, ErrorCode::Database);
    EXPECT_EQ(tactics.deleteWithSeq(kMatch, 1).error().code, ErrorCode::Database);
}

TEST_F(MatchTacticRepositoryTest, ChangesGoWithTheirMatch) {
    auto tactics = repository();
    ASSERT_TRUE(tactics.create(kMatch, change(10, std::nullopt, "4-4-2", "4-4-2"), 1));
    ASSERT_TRUE(sql("DELETE FROM match WHERE id = 1"));
    EXPECT_EQ(count("SELECT count(*) FROM match_tactic_change"), 0);
}
