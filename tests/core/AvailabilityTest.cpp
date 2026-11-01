#include "core/domain/Availability.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <vector>

using namespace fdss::core;
using namespace std::chrono;

namespace {

constexpr Date kFrom = 2026y / November / 10d;
constexpr Date kDayBefore = 2026y / November / 9d;
constexpr Date kExpected = 2026y / November / 20d;
constexpr Date kDayAfterExpected = 2026y / November / 21d;
constexpr Date kMuchLater = 2027y / June / 1d;

Availability entry(AvailabilityType type, Date from = kFrom,
                   std::optional<Date> expectedTo = std::nullopt,
                   std::optional<Date> actualTo = std::nullopt) {
    Availability result;
    result.type = type;
    result.fromDate = from;
    result.expectedTo = expectedTo;
    result.actualTo = actualTo;
    return result;
}

} // namespace

TEST(AvailabilityTest, NotActiveBeforeItStarts) {
    for (const AvailabilityType type : allValues<AvailabilityType>()) {
        EXPECT_FALSE(isActiveOn(entry(type), kDayBefore)) << toDbText(type);
        EXPECT_TRUE(isActiveOn(entry(type), kFrom)) << toDbText(type);
    }
}

TEST(AvailabilityTest, WithoutEndDatesEveryTypeStaysActive) {
    for (const AvailabilityType type : allValues<AvailabilityType>()) {
        EXPECT_TRUE(isActiveOn(entry(type), kMuchLater)) << toDbText(type);
    }
}

// expected_to is an estimate here: only marking the return ends the entry.
TEST(AvailabilityTest, InjuryAndIllnessIgnoreTheExpectedEnd) {
    for (const AvailabilityType type : {AvailabilityType::Injury, AvailabilityType::Illness}) {
        const Availability open = entry(type, kFrom, kExpected);
        EXPECT_TRUE(isActiveOn(open, kExpected)) << toDbText(type);
        EXPECT_TRUE(isActiveOn(open, kDayAfterExpected)) << toDbText(type);
        EXPECT_TRUE(isActiveOn(open, kMuchLater)) << toDbText(type);
    }
}

// expected_to is a known end here: the entry covers that day and ends after it.
TEST(AvailabilityTest, SuspensionAndAbsenceLastThroughTheExpectedEnd) {
    for (const AvailabilityType type : {AvailabilityType::Suspension, AvailabilityType::Absence}) {
        const Availability open = entry(type, kFrom, kExpected);
        EXPECT_TRUE(isActiveOn(open, kExpected)) << toDbText(type);
        EXPECT_FALSE(isActiveOn(open, kDayAfterExpected)) << toDbText(type);
    }
}

TEST(AvailabilityTest, ActualEndIsTheFirstAvailableDayForEveryType) {
    constexpr Date kReturned = 2026y / November / 15d;
    constexpr Date kLastDayOut = 2026y / November / 14d;
    for (const AvailabilityType type : allValues<AvailabilityType>()) {
        const Availability closed = entry(type, kFrom, std::nullopt, kReturned);
        EXPECT_TRUE(isActiveOn(closed, kLastDayOut)) << toDbText(type);
        EXPECT_FALSE(isActiveOn(closed, kReturned)) << toDbText(type);
    }
}

TEST(AvailabilityTest, ActualEndTakesPrecedenceOverTheExpectedEnd) {
    constexpr Date kEarlier = 2026y / November / 15d;
    constexpr Date kLater = 2026y / November / 25d;
    const Availability shortened = entry(AvailabilityType::Suspension, kFrom, kExpected, kEarlier);
    EXPECT_FALSE(isActiveOn(shortened, kEarlier));
    EXPECT_FALSE(isActiveOn(shortened, kExpected));

    const Availability extended = entry(AvailabilityType::Absence, kFrom, kExpected, kLater);
    EXPECT_TRUE(isActiveOn(extended, kDayAfterExpected));
    EXPECT_FALSE(isActiveOn(extended, kLater));
}

TEST(AvailabilityTest, EntryClosedOnItsFirstDayIsNeverActive) {
    const Availability closed = entry(AvailabilityType::Injury, kFrom, std::nullopt, kFrom);
    EXPECT_FALSE(isActiveOn(closed, kFrom));
    EXPECT_FALSE(isActiveOn(closed, kDayBefore));
}

TEST(AvailabilityTest, NoEntriesMeansAvailable) {
    EXPECT_EQ(playerStatusOn({}, kFrom), PlayerStatus::Available);
}

TEST(AvailabilityTest, EachTypeGivesItsStatus) {
    const auto statusOf = [](AvailabilityType type) {
        const std::vector entries{entry(type)};
        return playerStatusOn(entries, kFrom);
    };
    EXPECT_EQ(statusOf(AvailabilityType::Injury), PlayerStatus::Injured);
    EXPECT_EQ(statusOf(AvailabilityType::Illness), PlayerStatus::Ill);
    EXPECT_EQ(statusOf(AvailabilityType::Suspension), PlayerStatus::Suspended);
    EXPECT_EQ(statusOf(AvailabilityType::Absence), PlayerStatus::Absent);
}

TEST(AvailabilityTest, FinishedEntriesDoNotCount) {
    const std::vector entries{entry(AvailabilityType::Injury, kFrom, std::nullopt, kExpected)};
    EXPECT_EQ(playerStatusOn(entries, kDayBefore), PlayerStatus::Available);
    EXPECT_EQ(playerStatusOn(entries, kFrom), PlayerStatus::Injured);
    EXPECT_EQ(playerStatusOn(entries, kExpected), PlayerStatus::Available);
}

TEST(AvailabilityTest, OverlappingEntriesFollowThePriority) {
    const Availability injury = entry(AvailabilityType::Injury);
    const Availability illness = entry(AvailabilityType::Illness);
    const Availability suspension = entry(AvailabilityType::Suspension);
    const Availability absence = entry(AvailabilityType::Absence);

    // Listed against the priority, so the order of the entries cannot decide.
    std::vector entries{absence, suspension, illness, injury};
    EXPECT_EQ(playerStatusOn(entries, kFrom), PlayerStatus::Injured);
    entries.pop_back();
    EXPECT_EQ(playerStatusOn(entries, kFrom), PlayerStatus::Ill);
    entries.pop_back();
    EXPECT_EQ(playerStatusOn(entries, kFrom), PlayerStatus::Suspended);
    entries.pop_back();
    EXPECT_EQ(playerStatusOn(entries, kFrom), PlayerStatus::Absent);
}

TEST(AvailabilityTest, LowerPriorityEntryShowsOnceTheHigherOneEnds) {
    constexpr Date kReturned = 2026y / November / 15d;
    const std::vector entries{entry(AvailabilityType::Injury, kFrom, std::nullopt, kReturned),
                              entry(AvailabilityType::Suspension, kFrom, kExpected)};
    EXPECT_EQ(playerStatusOn(entries, kFrom), PlayerStatus::Injured);
    EXPECT_EQ(playerStatusOn(entries, kReturned), PlayerStatus::Suspended);
    EXPECT_EQ(playerStatusOn(entries, kExpected), PlayerStatus::Suspended);
    EXPECT_EQ(playerStatusOn(entries, kDayAfterExpected), PlayerStatus::Available);
}
