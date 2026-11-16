#include "core/domain/Lineup.hpp"
#include "core/algorithms/Formations.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <vector>

using namespace fdss::core;

namespace {

constexpr PlayerId kA{1};
constexpr PlayerId kB{2};
constexpr PlayerId kC{3};

Lineup empty442() { return emptyLineup(*findFormation("4-4-2")); }

std::vector<PlayerId> benchOf(std::initializer_list<std::int64_t> ids) {
    std::vector<PlayerId> bench;
    for (const std::int64_t id : ids) {
        bench.push_back(PlayerId{id});
    }
    return bench;
}

} // namespace

TEST(LineupTest, FindPlayerLooksAtTheSlotsThenTheBench) {
    Lineup lineup = empty442();
    lineup.pitchSlots[4].player = kA;
    lineup.bench = {kB};
    EXPECT_EQ(findPlayer(lineup, kA), (LineupPlace{LineupPlace::Kind::Slot, 4}));
    EXPECT_EQ(findPlayer(lineup, kB), (LineupPlace{LineupPlace::Kind::Bench, 0}));
    EXPECT_EQ(findPlayer(lineup, kC), std::nullopt);
    EXPECT_EQ(startingCount(lineup), 1);
}

TEST(LineupTest, AssignFromOutsidePushesTheOccupantOut) {
    Lineup lineup = empty442();
    EXPECT_EQ(assignToSlot(lineup, 0, kA), LineupEdit::Done);
    EXPECT_EQ(lineup.pitchSlots[0].player, kA);
    EXPECT_EQ(assignToSlot(lineup, 0, kB), LineupEdit::Done);
    EXPECT_EQ(lineup.pitchSlots[0].player, kB);
    EXPECT_EQ(findPlayer(lineup, kA), std::nullopt);
    EXPECT_TRUE(lineup.bench.empty());
}

TEST(LineupTest, AssignFromAnotherSlotSwaps) {
    Lineup lineup = empty442();
    lineup.pitchSlots[1].player = kA;
    lineup.pitchSlots[2].player = kB;
    EXPECT_EQ(assignToSlot(lineup, 2, kA), LineupEdit::Done);
    EXPECT_EQ(lineup.pitchSlots[1].player, kB);
    EXPECT_EQ(lineup.pitchSlots[2].player, kA);

    // Into a free slot: the old one becomes free.
    EXPECT_EQ(assignToSlot(lineup, 5, kA), LineupEdit::Done);
    EXPECT_EQ(lineup.pitchSlots[2].player, std::nullopt);
    EXPECT_EQ(lineup.pitchSlots[5].player, kA);
}

TEST(LineupTest, AssignFromTheBenchGivesTheOccupantThatBenchPlace) {
    Lineup lineup = empty442();
    lineup.pitchSlots[3].player = kA;
    lineup.bench = {kC, kB};
    EXPECT_EQ(assignToSlot(lineup, 3, kB), LineupEdit::Done);
    EXPECT_EQ(lineup.pitchSlots[3].player, kB);
    EXPECT_EQ(lineup.bench, (std::vector{kC, kA}));

    // Into a free slot: the bench place goes away.
    EXPECT_EQ(assignToSlot(lineup, 4, kC), LineupEdit::Done);
    EXPECT_EQ(lineup.bench, (std::vector{kA}));
}

TEST(LineupTest, AssignRejectsNothingButAnInvalidSlot) {
    Lineup lineup = empty442();
    lineup.pitchSlots[0].player = kA;
    const Lineup before = lineup;
    EXPECT_EQ(assignToSlot(lineup, 0, kA), LineupEdit::Unchanged);
    EXPECT_EQ(assignToSlot(lineup, -1, kB), LineupEdit::InvalidSlot);
    EXPECT_EQ(assignToSlot(lineup, kLineupSlots, kB), LineupEdit::InvalidSlot);
    EXPECT_EQ(lineup, before);
}

TEST(LineupTest, AddToBenchFromOutsideAndFromASlot) {
    Lineup lineup = empty442();
    lineup.pitchSlots[6].player = kA;
    EXPECT_EQ(addToBench(lineup, kB), LineupEdit::Done);
    EXPECT_EQ(addToBench(lineup, kA), LineupEdit::Done);
    EXPECT_EQ(lineup.pitchSlots[6].player, std::nullopt);
    EXPECT_EQ(lineup.bench, (std::vector{kB, kA}));
    EXPECT_EQ(addToBench(lineup, kA), LineupEdit::Unchanged);
}

TEST(LineupTest, TheBenchHoldsTwelve) {
    Lineup lineup = empty442();
    lineup.bench = benchOf({20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31});
    ASSERT_EQ(std::ssize(lineup.bench), kMaxBench);
    lineup.pitchSlots[9].player = kA;
    const Lineup before = lineup;
    EXPECT_EQ(addToBench(lineup, kB), LineupEdit::BenchFull);
    EXPECT_EQ(addToBench(lineup, kA), LineupEdit::BenchFull);
    EXPECT_EQ(moveSlotToBench(lineup, 9), LineupEdit::BenchFull);
    EXPECT_EQ(lineup, before);
}

TEST(LineupTest, SwapExchangesPlayersNotPositions) {
    Lineup lineup = empty442();
    lineup.pitchSlots[0].player = kA;
    lineup.pitchSlots[9].player = kB;
    EXPECT_EQ(swapSlots(lineup, 0, 9), LineupEdit::Done);
    EXPECT_EQ(lineup.pitchSlots[0].player, kB);
    EXPECT_EQ(lineup.pitchSlots[0].position, Position::Gk);
    EXPECT_EQ(lineup.pitchSlots[9].player, kA);
    EXPECT_EQ(lineup.pitchSlots[9].position, Position::St);
    EXPECT_EQ(swapSlots(lineup, 3, 4), LineupEdit::Unchanged); // both free
    EXPECT_EQ(swapSlots(lineup, 0, 11), LineupEdit::InvalidSlot);
}

TEST(LineupTest, MoveSlotToBench) {
    Lineup lineup = empty442();
    lineup.pitchSlots[2].player = kA;
    EXPECT_EQ(moveSlotToBench(lineup, 2), LineupEdit::Done);
    EXPECT_EQ(lineup.pitchSlots[2].player, std::nullopt);
    EXPECT_EQ(lineup.bench, (std::vector{kA}));
    EXPECT_EQ(moveSlotToBench(lineup, 2), LineupEdit::Unchanged);
    EXPECT_EQ(moveSlotToBench(lineup, 12), LineupEdit::InvalidSlot);
}

TEST(LineupTest, RemoveTakesEveryPlace) {
    Lineup lineup = empty442();
    lineup.pitchSlots[1].player = kA;
    lineup.pitchSlots[2].player = kA; // a duplicate from a file
    lineup.bench = {kB, kA};
    EXPECT_EQ(removePlayer(lineup, kA), LineupEdit::Done);
    EXPECT_EQ(findPlayer(lineup, kA), std::nullopt);
    EXPECT_EQ(lineup.bench, (std::vector{kB}));
    EXPECT_EQ(removePlayer(lineup, kA), LineupEdit::Unchanged);
}

TEST(LineupTest, ClearKeepsTheShape) {
    Lineup lineup = empty442();
    ASSERT_EQ(moveSlot(lineup, 8, Position::Lw), LineupEdit::Done);
    lineup.pitchSlots[0].player = kA;
    lineup.bench = {kB};
    const std::vector<LineupSlot> shape = [&] {
        auto slots = lineup.pitchSlots;
        for (auto& slot : slots) {
            slot.player.reset();
        }
        return slots;
    }();
    clearPlayers(lineup);
    EXPECT_EQ(lineup.pitchSlots, shape);
    EXPECT_TRUE(lineup.bench.empty());
    EXPECT_EQ(startingCount(lineup), 0);
}
