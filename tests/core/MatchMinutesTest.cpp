// Match Day minutes and clock (packet P13). Expected values are counted by hand from the events.

#include "core/algorithms/MatchMinutes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <vector>

using namespace fdss::core;
using std::chrono::minutes;
using std::chrono::seconds;
using namespace std::chrono_literals;

namespace {

constexpr int kFull = 90;

PlayerId pid(std::int64_t id) { return PlayerId{id}; }

// Starters 1–11 (1 is the goalkeeper, 2–11 central midfielders for simplicity), bench 12–14.
MatchSheet sheet() {
    MatchSheet result;
    result.starters.push_back({pid(1), Position::Gk});
    for (std::int64_t id = 2; id <= 11; ++id) {
        result.starters.push_back({pid(id), id == 5 ? Position::Lb : Position::Cm});
    }
    result.bench = {pid(12), pid(13), pid(14)};
    return result;
}

MatchEventDraft goal(int minute, std::int64_t scorer, std::optional<std::int64_t> assist = {}) {
    MatchEventDraft event{.minute = minute, .type = MatchEventType::Goal, .playerId = pid(scorer)};
    if (assist) {
        event.relatedPlayerId = pid(*assist);
    }
    return event;
}

MatchEventDraft sub(int minute, std::int64_t in, std::int64_t out) {
    return {.minute = minute,
            .type = MatchEventType::Sub,
            .playerId = pid(in),
            .relatedPlayerId = pid(out)};
}

MatchEventDraft card(MatchEventType type, int minute, std::int64_t player) {
    return {.minute = minute, .type = type, .playerId = pid(player)};
}

const AppearanceDraft* appearanceOf(const MatchMinutes& minutes, std::int64_t player) {
    for (const AppearanceDraft& appearance : minutes.appearances) {
        if (appearance.playerId == pid(player)) {
            return &appearance;
        }
    }
    return nullptr;
}

void expectMinutes(const MatchMinutes& minutes, std::int64_t player, int on, int off) {
    SCOPED_TRACE(player);
    const AppearanceDraft* appearance = appearanceOf(minutes, player);
    ASSERT_NE(appearance, nullptr);
    EXPECT_EQ(appearance->minuteOn, on);
    EXPECT_EQ(appearance->minuteOff, off);
}

const Timestamp kKickoff = std::chrono::sys_days{std::chrono::year{2026} / 11 / 1} + 14h;

MatchClock firstHalfClock(int halfLength = 45) {
    MatchClock clock;
    clock.halfLength = halfLength;
    clock.firstHalfStart = kKickoff;
    return clock;
}

MatchClock secondHalfClock(int halfLength = 45) {
    MatchClock clock = firstHalfClock(halfLength);
    clock.firstHalfEnd = kKickoff + minutes{halfLength + 2};
    clock.secondHalfStart = kKickoff + minutes{halfLength + 17};
    return clock;
}

MatchTime timeAt(const MatchClock& clock, seconds sinceHalfStart) {
    const Timestamp start = clock.secondHalfStart ? *clock.secondHalfStart : *clock.firstHalfStart;
    return readClock(clock, start + sinceHalfStart).eventTime;
}

} // namespace

TEST(MatchMinutesTest, StartersPlayTheWholeMatch) {
    const auto minutes = computeMatchMinutes(sheet(), {}, kFull);
    ASSERT_TRUE(minutes.has_value());
    ASSERT_EQ(minutes->appearances.size(), 11U);
    for (const AppearanceDraft& appearance : minutes->appearances) {
        EXPECT_TRUE(appearance.started);
        EXPECT_EQ(appearance.minuteOn, 0);
        EXPECT_EQ(appearance.minuteOff, kFull);
    }
    EXPECT_EQ(minutes->appearances.front().position, Position::Gk);
    // Unused substitutes get no appearance.
    EXPECT_EQ(appearanceOf(*minutes, 12), nullptr);
}

TEST(MatchMinutesTest, RunningMatchLeavesTheEndOpen) {
    const std::vector events{sub(60, 12, 5)};
    const auto minutes = computeMatchMinutes(sheet(), events, std::nullopt);
    ASSERT_TRUE(minutes.has_value());
    EXPECT_EQ(appearanceOf(*minutes, 2)->minuteOff, std::nullopt);
    EXPECT_EQ(appearanceOf(*minutes, 5)->minuteOff, 60);
    EXPECT_EQ(appearanceOf(*minutes, 12)->minuteOn, 60);
    EXPECT_EQ(appearanceOf(*minutes, 12)->minuteOff, std::nullopt);
}

TEST(MatchMinutesTest, SubstitutePlaysFromTheMinuteAndTakesThePosition) {
    const std::vector events{sub(60, 12, 5)};
    const auto minutes = computeMatchMinutes(sheet(), events, kFull);
    ASSERT_TRUE(minutes.has_value());
    expectMinutes(*minutes, 5, 0, 60);   // 60 minutes
    expectMinutes(*minutes, 12, 60, 90); // 30 minutes
    EXPECT_FALSE(appearanceOf(*minutes, 12)->started);
    EXPECT_EQ(appearanceOf(*minutes, 12)->position, Position::Lb);
    EXPECT_EQ(minutes->appearances.size(), 12U);
}

TEST(MatchMinutesTest, SubstituteOfASubstitute) {
    const std::vector events{sub(60, 12, 5), sub(80, 13, 12)};
    const auto minutes = computeMatchMinutes(sheet(), events, kFull);
    ASSERT_TRUE(minutes.has_value());
    expectMinutes(*minutes, 5, 0, 60);
    expectMinutes(*minutes, 12, 60, 80); // 20 minutes
    expectMinutes(*minutes, 13, 80, 90); // 10 minutes
    EXPECT_EQ(appearanceOf(*minutes, 13)->position, Position::Lb);
}

TEST(MatchMinutesTest, RedCardEndsTheMinutesAndCannotBeSubstituted) {
    const std::vector events{card(MatchEventType::Red, 30, 7)};
    const auto minutes = computeMatchMinutes(sheet(), events, kFull);
    ASSERT_TRUE(minutes.has_value());
    expectMinutes(*minutes, 7, 0, 30);

    const std::vector invalid{card(MatchEventType::Red, 30, 7), sub(40, 12, 7)};
    const auto rejected = computeMatchMinutes(sheet(), invalid, kFull);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error(), (MatchEventError{MatchEventProblem::SentOff, 1}));
}

TEST(MatchMinutesTest, SecondYellowIsARedRowAtTheSameMinute) {
    const std::vector events{card(MatchEventType::Yellow, 20, 3),
                             card(MatchEventType::Yellow, 50, 3), card(MatchEventType::Red, 50, 3)};
    const auto situation = replayMatch(sheet(), events, kFull);
    ASSERT_TRUE(situation.has_value());
    EXPECT_EQ(situation->find(pid(3))->yellowCards, 2);
    EXPECT_EQ(situation->find(pid(3))->state, PlayerMatchState::SentOff);
    expectMinutes(minutesOf(*situation, kFull), 3, 0, 50);
}

TEST(MatchMinutesTest, StoppageTimeIsCutAtTheEndOfTheHalf) {
    // A yellow at 45+2 and a half-time substitution are both stored at minute 45; a goal at 90+3
    // at minute 90. The whole match is 90 minutes whatever the stoppage time.
    MatchEventDraft yellow = card(MatchEventType::Yellow, 45, 4);
    yellow.addedTime = 2;
    MatchEventDraft late = goal(90, 12);
    late.addedTime = 3;
    const std::vector events{yellow, sub(45, 12, 4), late};
    const auto minutes = computeMatchMinutes(sheet(), events, kFull);
    ASSERT_TRUE(minutes.has_value());
    expectMinutes(*minutes, 4, 0, 45);
    expectMinutes(*minutes, 12, 45, 90);
    expectMinutes(*minutes, 2, 0, 90);
    EXPECT_EQ(minutes->goalsFor, 1);
}

TEST(MatchMinutesTest, EventsAfterTheEndAreRejected) {
    const std::vector events{goal(10, 9), goal(91, 9)};
    const auto rejected = computeMatchMinutes(sheet(), events, kFull);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error(), (MatchEventError{MatchEventProblem::AfterFinalMinute, 1}));
    // While the match runs there is no end yet.
    EXPECT_TRUE(computeMatchMinutes(sheet(), events, std::nullopt).has_value());
}

TEST(MatchMinutesTest, InvalidSubstitutions) {
    const auto problemOf = [](std::vector<MatchEventDraft> events) {
        const auto result = computeMatchMinutes(sheet(), events, kFull);
        return result ? std::nullopt : std::optional{result.error().problem};
    };
    // The player going off has already gone off.
    EXPECT_EQ(problemOf({sub(60, 12, 5), sub(70, 13, 5)}), MatchEventProblem::AlreadySubstituted);
    // A substituted player cannot come back.
    EXPECT_EQ(problemOf({sub(60, 12, 5), sub(70, 5, 12)}), MatchEventProblem::AlreadySubstituted);
    // The player coming on is already on the pitch.
    EXPECT_EQ(problemOf({sub(60, 2, 5)}), MatchEventProblem::NotOnBench);
    // The player going off is on the bench.
    EXPECT_EQ(problemOf({sub(60, 12, 13)}), MatchEventProblem::NotOnPitch);
    EXPECT_EQ(problemOf({sub(60, 12, 12)}), MatchEventProblem::SamePlayer);
    EXPECT_EQ(problemOf({sub(60, 99, 5)}), MatchEventProblem::UnknownPlayer);
    EXPECT_EQ(problemOf({{.minute = 60, .type = MatchEventType::Sub, .playerId = pid(12)}}),
              MatchEventProblem::MissingPlayer);
}

TEST(MatchMinutesTest, ScoreGoalsAndAssists) {
    const std::vector events{goal(10, 9, 10),
                             MatchEventDraft{.minute = 20, .type = MatchEventType::OpponentGoal},
                             goal(70, 10)};
    const auto situation = replayMatch(sheet(), events, kFull);
    ASSERT_TRUE(situation.has_value());
    EXPECT_EQ(situation->goalsFor, 2);
    EXPECT_EQ(situation->goalsAgainst, 1);
    EXPECT_EQ(situation->find(pid(9))->goals, 1);
    EXPECT_EQ(situation->find(pid(10))->goals, 1);
    EXPECT_EQ(situation->find(pid(10))->assists, 1);

    const MatchSituation start = startingSituation(sheet());
    EXPECT_EQ(checkEvent(start, goal(10, 9, 9)), MatchEventProblem::SamePlayer);
    EXPECT_EQ(checkEvent(start, goal(10, 12)), MatchEventProblem::NotOnPitch);
    EXPECT_EQ(checkEvent(start, goal(10, 9, 12)), MatchEventProblem::NotOnPitch);
    EXPECT_EQ(checkEvent(start, card(MatchEventType::Yellow, 10, 13)),
              MatchEventProblem::NotOnPitch);
    EXPECT_EQ(checkEvent(start, goal(131, 9)), MatchEventProblem::MinuteOutOfRange);
    MatchEventDraft zeroAdded = goal(45, 9);
    zeroAdded.addedTime = 0;
    EXPECT_EQ(checkEvent(start, zeroAdded), MatchEventProblem::MinuteOutOfRange);
    // checkEvent changes nothing.
    EXPECT_EQ(checkEvent(start, goal(10, 9)), std::nullopt);
    EXPECT_EQ(start.goalsFor, 0);
}

TEST(MatchMinutesTest, SheetFromLineupSkipsEmptySlots) {
    Lineup lineup;
    lineup.pitchSlots = {{Position::Gk, 0.5, 0.95, pid(1)},
                         {Position::Cb, 0.4, 0.8, std::nullopt},
                         {Position::St, 0.5, 0.1, pid(9)}};
    lineup.bench = {pid(12)};
    const MatchSheet result = matchSheetOf(lineup);
    EXPECT_EQ(result.starters,
              (std::vector<StartingPlayer>{{pid(1), Position::Gk}, {pid(9), Position::St}}));
    EXPECT_EQ(result.bench, std::vector{pid(12)});
}

TEST(MatchClockTest, DefaultHalfLengthByCategory) {
    EXPECT_EQ(defaultHalfLength(TeamCategory::Senior), 45);
    EXPECT_EQ(defaultHalfLength(TeamCategory::U19), 45);
    EXPECT_EQ(defaultHalfLength(TeamCategory::U17), 40);
    EXPECT_EQ(defaultHalfLength(TeamCategory::U15), 35);
    EXPECT_EQ(defaultHalfLength(TeamCategory::U13), 30);
    EXPECT_EQ(defaultHalfLength(TeamCategory::U11), 25);
    EXPECT_EQ(defaultHalfLength(TeamCategory::Other), 45);
    EXPECT_TRUE(isValidHalfLength(10));
    EXPECT_TRUE(isValidHalfLength(60));
    EXPECT_FALSE(isValidHalfLength(9));
    EXPECT_FALSE(isValidHalfLength(61));
}

TEST(MatchClockTest, StepsInOrder) {
    MatchClock clock;
    EXPECT_EQ(phaseOf(clock), MatchPhase::NotStarted);
    EXPECT_EQ(nextStep(clock), ClockStep::FirstHalfStart);
    EXPECT_EQ(lastStep(clock), std::nullopt);
    setStep(clock, ClockStep::FirstHalfStart, kKickoff);
    EXPECT_EQ(phaseOf(clock), MatchPhase::FirstHalf);
    setStep(clock, ClockStep::FirstHalfEnd, kKickoff + 46min);
    EXPECT_EQ(phaseOf(clock), MatchPhase::HalfTime);
    setStep(clock, ClockStep::SecondHalfStart, kKickoff + 60min);
    EXPECT_EQ(phaseOf(clock), MatchPhase::SecondHalf);
    EXPECT_EQ(nextStep(clock), ClockStep::MatchEnd);
    setStep(clock, ClockStep::MatchEnd, kKickoff + 107min);
    EXPECT_EQ(phaseOf(clock), MatchPhase::Finished);
    EXPECT_EQ(nextStep(clock), std::nullopt);
    EXPECT_EQ(lastStep(clock), ClockStep::MatchEnd);
    setStep(clock, ClockStep::MatchEnd, std::nullopt);
    EXPECT_EQ(lastStep(clock), ClockStep::SecondHalfStart);
}

TEST(MatchClockTest, FirstHalfMinutesAndDisplay) {
    const MatchClock clock = firstHalfClock();
    // 00:30 → stored 0, shown 1'.
    EXPECT_EQ(timeAt(clock, 30s), (MatchTime{0, std::nullopt}));
    EXPECT_EQ(displayMinute(timeAt(clock, 30s)), (DisplayMinute{1, std::nullopt}));
    // 44:20 → stored 44, shown 45'.
    EXPECT_EQ(timeAt(clock, 44min + 20s), (MatchTime{44, std::nullopt}));
    EXPECT_EQ(displayMinute(timeAt(clock, 44min + 20s)), (DisplayMinute{45, std::nullopt}));
    // 45:30 in stoppage time → stored 45 + 1, shown 45+1'.
    EXPECT_EQ(timeAt(clock, 45min + 30s), (MatchTime{45, 1}));
    EXPECT_EQ(displayMinute(timeAt(clock, 45min + 30s)), (DisplayMinute{45, 1}));
    // 47:10 → stored 45 + 3, shown 45+3'.
    EXPECT_EQ(timeAt(clock, 47min + 10s), (MatchTime{45, 3}));
    EXPECT_EQ(displayMinute(timeAt(clock, 47min + 10s)), (DisplayMinute{45, 3}));

    const ClockReading reading = readClock(clock, kKickoff + 47min + 10s);
    EXPECT_EQ(reading.phase, MatchPhase::FirstHalf);
    EXPECT_EQ(reading.regular, 45min);
    EXPECT_EQ(reading.stoppage, 2min + 10s);
    EXPECT_EQ(readClock(clock, kKickoff + 23min + 41s).regular, 23min + 41s);
    EXPECT_EQ(readClock(clock, kKickoff + 23min + 41s).stoppage, 0s);
}

TEST(MatchClockTest, SecondHalfMinutesAndDisplay) {
    const MatchClock clock = secondHalfClock();
    // Kick-off of the second half → stored 45, shown 46'.
    EXPECT_EQ(timeAt(clock, 0s), (MatchTime{45, std::nullopt}));
    EXPECT_EQ(displayMinute(timeAt(clock, 0s)), (DisplayMinute{46, std::nullopt}));
    // 44:59 into the half → stored 89, shown 90'.
    EXPECT_EQ(timeAt(clock, 44min + 59s), (MatchTime{89, std::nullopt}));
    EXPECT_EQ(displayMinute(timeAt(clock, 44min + 59s)), (DisplayMinute{90, std::nullopt}));
    EXPECT_EQ(timeAt(clock, 45min), (MatchTime{90, 1}));
    EXPECT_EQ(displayMinute(timeAt(clock, 47min + 10s)), (DisplayMinute{90, 3}));
    const ClockReading reading = readClock(clock, *clock.secondHalfStart + 47min + 10s);
    EXPECT_EQ(reading.regular, 90min);
    EXPECT_EQ(reading.stoppage, 2min + 10s);
    EXPECT_EQ(readClock(clock, *clock.secondHalfStart + 20min).regular, 65min);
}

TEST(MatchClockTest, ShortHalvesOfYouthTeams) {
    const MatchClock clock = secondHalfClock(25);
    EXPECT_EQ(timeAt(clock, 10min), (MatchTime{35, std::nullopt}));
    EXPECT_EQ(timeAt(clock, 26min), (MatchTime{50, 2}));
}

TEST(MatchClockTest, BreakEndAndBeforeKickoff) {
    MatchClock clock = firstHalfClock();
    clock.firstHalfEnd = kKickoff + 47min;
    const ClockReading halfTime = readClock(clock, kKickoff + 55min);
    EXPECT_EQ(halfTime.phase, MatchPhase::HalfTime);
    EXPECT_EQ(halfTime.regular, 45min);
    EXPECT_EQ(halfTime.stoppage, 0s);
    EXPECT_EQ(halfTime.eventTime, (MatchTime{45, std::nullopt}));

    clock = secondHalfClock();
    clock.matchEnd = *clock.secondHalfStart + 49min;
    const ClockReading finished = readClock(clock, *clock.matchEnd + 3h);
    EXPECT_EQ(finished.phase, MatchPhase::Finished);
    EXPECT_EQ(finished.regular, 90min);
    EXPECT_EQ(finished.eventTime, (MatchTime{90, std::nullopt}));

    EXPECT_EQ(readClock(MatchClock{}, kKickoff).regular, 0s);
    // The system clock went back: no negative time.
    EXPECT_EQ(readClock(firstHalfClock(), kKickoff - 5min).regular, 0s);
    EXPECT_EQ(timeAt(firstHalfClock(), -5min), (MatchTime{0, std::nullopt}));
}

TEST(MatchClockTest, ResumesFromThePersistedStart) {
    // The app closed at 20:00 and opened 5 minutes later: the clock is derived, so it shows 25:00.
    const MatchClock clock = firstHalfClock();
    EXPECT_EQ(readClock(clock, kKickoff + 25min).regular, 25min);
}
