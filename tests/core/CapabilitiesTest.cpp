#include "core/algorithms/Capabilities.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <vector>

using namespace fdss::core;

namespace {

// Every condition holds with the defaults.
DataCounts enough() {
    DataCounts counts;
    counts.activePlayers = 11;
    counts.finishedMatches = 1;
    counts.rpeDays = 14;
    counts.daysSinceFirstRpe = 28;
    counts.ratingEntries = 1;
    counts.matchesWithResult = 3;
    counts.eventFiles = 1;
    return counts;
}

Capability of(const DataCounts& counts, CapabilityKind kind, bool showAdvanced = false) {
    const std::vector<Capability> all = evaluateCapabilities(counts, {}, showAdvanced);
    return all.at(static_cast<std::size_t>(kind));
}

} // namespace

TEST(Capabilities, OneEntryPerKindInOrder) {
    const std::vector<Capability> all = evaluateCapabilities({}, {}, false);
    ASSERT_EQ(all.size(), kCapabilityKinds.size());
    for (std::size_t i = 0; i < all.size(); ++i) {
        EXPECT_EQ(all[i].kind, kCapabilityKinds[i]);
    }
}

TEST(Capabilities, EverythingAvailableWithEnoughData) {
    for (const Capability& capability : evaluateCapabilities(enough(), {}, false)) {
        EXPECT_EQ(capability.state, CapabilityState::Available);
        EXPECT_EQ(capability.missing(), 0);
    }
}

TEST(Capabilities, SquadBoundary) {
    DataCounts counts = enough();
    counts.activePlayers = 10;
    const Capability squad = of(counts, CapabilityKind::Squad);
    EXPECT_EQ(squad.state, CapabilityState::Locked);
    EXPECT_EQ(squad.have, 10);
    EXPECT_EQ(squad.need, 11);
    EXPECT_EQ(squad.missing(), 1);
    counts.activePlayers = 11;
    EXPECT_EQ(of(counts, CapabilityKind::Squad).state, CapabilityState::Available);
}

TEST(Capabilities, EmptyClubNeedsElevenPlayers) {
    const Capability squad = of({}, CapabilityKind::Squad);
    EXPECT_EQ(squad.state, CapabilityState::Locked);
    EXPECT_EQ(squad.missing(), 11);
}

TEST(Capabilities, MatchesBoundary) {
    DataCounts counts = enough();
    counts.finishedMatches = 0;
    EXPECT_EQ(of(counts, CapabilityKind::Matches).state, CapabilityState::Locked);
    EXPECT_EQ(of(counts, CapabilityKind::Matches).missing(), 1);
    counts.finishedMatches = 1;
    EXPECT_EQ(of(counts, CapabilityKind::Matches).state, CapabilityState::Available);
}

TEST(Capabilities, LoadBoundaryAndHintNumber) {
    DataCounts counts = enough();
    counts.rpeDays = 5;
    counts.daysSinceFirstRpe = 10;
    Capability load = of(counts, CapabilityKind::Load);
    EXPECT_EQ(load.state, CapabilityState::Locked);
    EXPECT_EQ(load.missing(), 9); // "Enter RPE on 9 more days"
    EXPECT_EQ(load.daysToFullLoad, std::nullopt);

    counts.rpeDays = 13;
    EXPECT_EQ(of(counts, CapabilityKind::Load).state, CapabilityState::Locked);
    EXPECT_EQ(of(counts, CapabilityKind::Load).missing(), 1);

    counts.rpeDays = 14;
    counts.daysSinceFirstRpe = 20;
    load = of(counts, CapabilityKind::Load);
    EXPECT_EQ(load.state, CapabilityState::Available);
    EXPECT_EQ(load.daysToFullLoad, 8);
}

TEST(Capabilities, FullLoadAfterTwentyEightDays) {
    DataCounts counts = enough();
    counts.daysSinceFirstRpe = 27;
    EXPECT_EQ(of(counts, CapabilityKind::Load).daysToFullLoad, 1);
    counts.daysSinceFirstRpe = 28;
    EXPECT_EQ(of(counts, CapabilityKind::Load).daysToFullLoad, std::nullopt);
}

TEST(Capabilities, RatingsBoundary) {
    DataCounts counts = enough();
    counts.ratingEntries = 0;
    EXPECT_EQ(of(counts, CapabilityKind::Ratings).state, CapabilityState::Locked);
    counts.ratingEntries = 1;
    EXPECT_EQ(of(counts, CapabilityKind::Ratings).state, CapabilityState::Available);
}

TEST(Capabilities, OpponentsBoundary) {
    DataCounts counts = enough();
    counts.matchesWithResult = 2;
    const Capability opponents = of(counts, CapabilityKind::Opponents);
    EXPECT_EQ(opponents.state, CapabilityState::Locked);
    EXPECT_EQ(opponents.missing(), 1);
    counts.matchesWithResult = 3;
    EXPECT_EQ(of(counts, CapabilityKind::Opponents).state, CapabilityState::Available);
}

TEST(Capabilities, EventDataHiddenWithoutFileUnlessAdvanced) {
    DataCounts counts = enough();
    counts.eventFiles = 0;
    EXPECT_EQ(of(counts, CapabilityKind::EventData, false).state, CapabilityState::Hidden);
    EXPECT_EQ(of(counts, CapabilityKind::EventData, true).state, CapabilityState::Locked);
    EXPECT_EQ(of(counts, CapabilityKind::EventData, true).missing(), 1);
    counts.eventFiles = 1;
    EXPECT_EQ(of(counts, CapabilityKind::EventData, false).state, CapabilityState::Available);
    EXPECT_EQ(of(counts, CapabilityKind::EventData, true).state, CapabilityState::Available);
}

TEST(Capabilities, ThresholdsArePassedIn) {
    CapabilityThresholds thresholds;
    thresholds.minPlayers = 7;
    DataCounts counts;
    counts.activePlayers = 7;
    const std::vector<Capability> all = evaluateCapabilities(counts, thresholds, false);
    EXPECT_EQ(all.front().state, CapabilityState::Available);
    EXPECT_EQ(all.front().need, 7);
}
