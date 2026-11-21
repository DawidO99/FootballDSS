// Match Day minutes and clock (packet P13). Expected values are counted by hand from the events.

#include "core/algorithms/MatchMinutes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <format>
#include <string>
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

// A clock whose parts started 15 minutes after the end of the one before; `ends` holds how long
// each part ended so far lasted (a part beyond them is running).
MatchClock clockOf(MatchFormat format, std::vector<seconds> ends, bool running) {
    MatchClock clock;
    clock.format = format;
    Timestamp at = kKickoff;
    for (const seconds length : ends) {
        clock.periods.push_back({at, at + length});
        at += length + 15min;
    }
    if (running) {
        clock.periods.push_back({at, std::nullopt});
    }
    return clock;
}

// The time of the part running now, `into` after its start.
MatchTime timeAt(const MatchClock& clock, seconds into) {
    return readClock(clock, clock.periods.back().start + into).eventTime;
}

std::string shown(MatchTime time) {
    const DisplayMinute display = displayMinute(time);
    return display.added ? std::format("{}+{}'", display.minute, *display.added)
                         : std::format("{}'", display.minute);
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

TEST(MatchClockTest, FormatsAndTheDefaultByCategory) {
    EXPECT_EQ(defaultMatchFormat(TeamCategory::Senior), (MatchFormat{2, 45}));
    EXPECT_EQ(defaultMatchFormat(TeamCategory::U19), (MatchFormat{2, 45}));
    EXPECT_EQ(defaultMatchFormat(TeamCategory::U17), (MatchFormat{2, 40}));
    EXPECT_EQ(defaultMatchFormat(TeamCategory::U15), (MatchFormat{2, 35}));
    EXPECT_EQ(defaultMatchFormat(TeamCategory::U13), (MatchFormat{2, 30}));
    EXPECT_EQ(defaultMatchFormat(TeamCategory::U11), (MatchFormat{2, 25}));
    EXPECT_EQ(defaultMatchFormat(TeamCategory::Other), (MatchFormat{2, 45}));
    for (const MatchFormat preset : kMatchFormatPresets) {
        EXPECT_TRUE(isValidMatchFormat(preset));
    }
    EXPECT_TRUE(isValidMatchFormat({1, 60}));
    EXPECT_TRUE(isValidMatchFormat({4, 30}));
    EXPECT_FALSE(isValidMatchFormat({0, 45}));
    EXPECT_FALSE(isValidMatchFormat({5, 20}));
    EXPECT_FALSE(isValidMatchFormat({2, 9}));
    EXPECT_FALSE(isValidMatchFormat({2, 61}));
    // The minutes of 0001 go up to 130.
    EXPECT_FALSE(isValidMatchFormat({4, 40}));
    EXPECT_EQ((MatchFormat{3, 30}).totalMinutes(), 90);
}

TEST(MatchClockTest, StepsInOrderForFourParts) {
    MatchClock clock;
    clock.format = {4, 20};
    std::vector<ClockStep> steps;
    Timestamp at = kKickoff;
    while (const auto step = nextStep(clock)) {
        steps.push_back(*step);
        applyStep(clock, *step, at);
        at += 25min;
        if (phaseOf(clock) == MatchPhase::Break) {
            EXPECT_EQ(nextStep(clock)->kind, ClockStepKind::Start);
        }
        if (step->period == 4 && step->kind == ClockStepKind::End) {
            clock.finished = true; // the end of the last part ends the match
        }
    }
    ASSERT_EQ(steps.size(), 8U);
    EXPECT_EQ(steps.front(), (ClockStep{1, ClockStepKind::Start}));
    EXPECT_EQ(steps[3], (ClockStep{2, ClockStepKind::End}));
    EXPECT_EQ(steps.back(), (ClockStep{4, ClockStepKind::End}));
    EXPECT_EQ(phaseOf(clock), MatchPhase::Finished);
    EXPECT_EQ(finalMinute(clock), 80);
    // A step out of order changes nothing.
    MatchClock fresh;
    applyStep(fresh, {2, ClockStepKind::Start}, kKickoff);
    EXPECT_EQ(phaseOf(fresh), MatchPhase::NotStarted);
}

TEST(MatchClockTest, HalvesMinutesAndDisplay) {
    const MatchClock first = clockOf({2, 45}, {}, true);
    EXPECT_EQ(timeAt(first, 30s), (MatchTime{0, std::nullopt}));
    EXPECT_EQ(shown(timeAt(first, 30s)), "1'");
    EXPECT_EQ(shown(timeAt(first, 44min + 20s)), "45'");
    EXPECT_EQ(timeAt(first, 45min + 30s), (MatchTime{45, 1}));
    EXPECT_EQ(shown(timeAt(first, 45min + 30s)), "45+1'");
    EXPECT_EQ(shown(timeAt(first, 47min + 10s)), "45+3'");
    const ClockReading reading = readClock(first, kKickoff + 47min + 10s);
    EXPECT_EQ(reading.phase, MatchPhase::Running);
    EXPECT_EQ(reading.period, 1);
    EXPECT_EQ(reading.regular, 45min);
    EXPECT_EQ(reading.stoppage, 2min + 10s);

    const MatchClock second = clockOf({2, 45}, {47min}, true);
    EXPECT_EQ(shown(timeAt(second, 0s)), "46'");
    EXPECT_EQ(shown(timeAt(second, 44min + 59s)), "90'");
    EXPECT_EQ(shown(timeAt(second, 45min)), "90+1'");
    EXPECT_EQ(readClock(second, second.periods.back().start + 20min).regular, 65min);
}

TEST(MatchClockTest, ThreePartsOfThirty) {
    const MatchClock first = clockOf({3, 30}, {}, true);
    EXPECT_EQ(shown(timeAt(first, 29min + 59s)), "30'");
    EXPECT_EQ(timeAt(first, 31min + 10s), (MatchTime{30, 2}));
    EXPECT_EQ(shown(timeAt(first, 31min + 10s)), "30+2'");
    EXPECT_EQ(readClock(first, kKickoff + 31min + 10s).regular, 30min);

    const MatchClock second = clockOf({3, 30}, {32min}, true);
    EXPECT_EQ(shown(timeAt(second, 0s)), "31'");
    EXPECT_EQ(timeAt(second, 30min + 30s), (MatchTime{60, 1}));
    EXPECT_EQ(shown(timeAt(second, 30min + 30s)), "60+1'");

    const MatchClock third = clockOf({3, 30}, {32min, 31min}, true);
    EXPECT_EQ(timeAt(third, 10min), (MatchTime{70, std::nullopt}));
    EXPECT_EQ(readClock(third, third.periods.back().start + 10min).regular, 70min);

    MatchClock finished = clockOf({3, 30}, {32min, 31min, 33min}, false);
    finished.finished = true;
    EXPECT_EQ(finalMinute(finished), 90);
    EXPECT_EQ(readClock(finished, kKickoff + 5h).eventTime, (MatchTime{90, std::nullopt}));
}

TEST(MatchClockTest, FourPartsOfTwenty) {
    const MatchClock fourth = clockOf({4, 20}, {21min, 20min, 22min}, true);
    EXPECT_EQ(readClock(fourth, fourth.periods.back().start).period, 4);
    EXPECT_EQ(shown(timeAt(fourth, 0s)), "61'");
    EXPECT_EQ(shown(timeAt(fourth, 19min)), "80'");
    EXPECT_EQ(timeAt(fourth, 20min + 30s), (MatchTime{80, 1}));
    EXPECT_EQ(shown(timeAt(fourth, 20min + 30s)), "80+1'");
    // A break after the second part: the clock stands at 40:00, a substitution is at 40 (41').
    const MatchClock inBreak = clockOf({4, 20}, {21min, 20min}, false);
    const ClockReading reading = readClock(inBreak, kKickoff + 70min);
    EXPECT_EQ(reading.phase, MatchPhase::Break);
    EXPECT_EQ(reading.period, 2);
    EXPECT_EQ(reading.regular, 40min);
    EXPECT_EQ(reading.eventTime, (MatchTime{40, std::nullopt}));
}

TEST(MatchClockTest, EndedEarly) {
    // Ended 10:30 into the third part of 3×30: the final minute is 70.
    const MatchClock third = clockOf({3, 30}, {31min, 30min}, true);
    const Timestamp at = third.periods.back().start + 10min + 30s;
    EXPECT_EQ(earlyEndMinute(third, at, {}), 70);
    MatchClock early = third;
    applyStep(early, {3, ClockStepKind::End}, at);
    early.finished = true;
    early.endedEarlyAt = 70;
    EXPECT_EQ(phaseOf(early), MatchPhase::Finished);
    EXPECT_EQ(finalMinute(early), 70);
    // Running in stoppage time: at most the end of the part.
    EXPECT_EQ(earlyEndMinute(third, third.periods.back().start + 33min, {}), 90);
    // A last part ended a little before its length by the clock button: still a whole match.
    MatchClock regular = clockOf({3, 30}, {31min, 30min, 29min + 50s}, false);
    regular.finished = true;
    EXPECT_EQ(finalMinute(regular), 90);
}

TEST(MatchClockTest, AnEarlyEndInABreakIsTheEndOfThePartPlayed) {
    // Codex: the first half ended at 44:30, a substitution in the break (at 45), then the end. The
    // final minute is the break, 45, not the clock at the end of the half (44).
    const MatchClock inBreak = clockOf({2, 45}, {44min + 30s}, false);
    const std::vector events{sub(45, 12, 5)};
    EXPECT_EQ(earlyEndMinute(inBreak, kKickoff + 50min, latestRecordedTime(events, {}, {})), 45);
    // Never before a stored event.
    const MatchClock running = clockOf({2, 45}, {}, true);
    const std::vector later{card(MatchEventType::Red, 30, 4)};
    EXPECT_EQ(earlyEndMinute(running, kKickoff + 20min, latestRecordedTime(later, {}, {})), 30);
}

TEST(MatchMinutesTest, TheSituationAtAMinuteReplaysUpToIt) {
    // 5 went off at 20 for 12; at 10 he was on the pitch and 12 on the bench.
    const std::vector events{goal(5, 9), sub(20, 12, 5), card(MatchEventType::Red, 25, 4)};
    const auto atTen = situationAt(sheet(), events, {10, std::nullopt});
    ASSERT_TRUE(atTen.has_value());
    EXPECT_EQ(atTen->find(pid(5))->state, PlayerMatchState::OnPitch);
    EXPECT_EQ(atTen->find(pid(12))->state, PlayerMatchState::Bench);
    EXPECT_EQ(atTen->goalsFor, 1);
    // At the minute of an event it is already applied: an event entered then comes after it.
    const auto atTwenty = situationAt(sheet(), events, {20, std::nullopt});
    EXPECT_EQ(atTwenty->find(pid(5))->state, PlayerMatchState::SubstitutedOff);
    EXPECT_EQ(atTwenty->find(pid(4))->state, PlayerMatchState::OnPitch);
}

TEST(MatchClockTest, BeforeKickoffAndClockGoingBack) {
    MatchClock clock;
    EXPECT_EQ(phaseOf(clock), MatchPhase::NotStarted);
    EXPECT_EQ(readClock(clock, kKickoff).regular, 0s);
    EXPECT_FALSE(isPlayedTime(clock, kKickoff, {0, std::nullopt}, false));
    const MatchClock running = clockOf({2, 45}, {}, true);
    EXPECT_EQ(readClock(running, kKickoff - 5min).regular, 0s);
    EXPECT_EQ(timeAt(running, -5min), (MatchTime{0, std::nullopt}));
}

TEST(MatchClockTest, ResumesFromThePersistedStart) {
    // The app closed at 20:00 and opened 5 minutes later: the clock is derived, so it shows 25:00.
    EXPECT_EQ(readClock(clockOf({2, 45}, {}, true), kKickoff + 25min).regular, 25min);
}

TEST(MatchClockTest, ShownMinutesAreParsed) {
    EXPECT_EQ(parseDisplayMinute("23"), (DisplayMinute{23, std::nullopt}));
    EXPECT_EQ(parseDisplayMinute(" 23' "), (DisplayMinute{23, std::nullopt}));
    EXPECT_EQ(parseDisplayMinute("45+2"), (DisplayMinute{45, 2}));
    EXPECT_EQ(parseDisplayMinute("45 + 2'"), (DisplayMinute{45, 2}));
    for (const char* bad : {"", "0", "x", "45+", "+2", "45+0", "-3", "4 5", "1000", "45+2+1"}) {
        EXPECT_EQ(parseDisplayMinute(bad), std::nullopt) << bad;
    }
    const MatchFormat halves{2, 45};
    EXPECT_EQ(matchTimeOf({23, std::nullopt}, halves), (MatchTime{22, std::nullopt}));
    EXPECT_EQ(matchTimeOf({45, 2}, halves), (MatchTime{45, 2}));
    EXPECT_EQ(matchTimeOf({90, 4}, halves), (MatchTime{90, 4}));
    EXPECT_EQ(matchTimeOf({44, 2}, halves), std::nullopt); // added time only at the end of a part
    EXPECT_EQ(matchTimeOf({135, 1}, halves), std::nullopt);
    EXPECT_EQ(matchTimeOf({91, std::nullopt}, halves), std::nullopt);
    EXPECT_EQ(matchTimeOf({30, 2}, {3, 30}), (MatchTime{30, 2}));
    EXPECT_EQ(matchTimeOf({60, 1}, {3, 30}), (MatchTime{60, 1}));
    EXPECT_EQ(matchTimeOf({45, 1}, {3, 30}), std::nullopt);
}

TEST(MatchClockTest, OnlyPlayedTimeCanBeEntered) {
    // Second half of 2×45, 15:00 in (the 61st minute); the first half lasted 47:10.
    const MatchClock clock = clockOf({2, 45}, {47min + 10s}, true);
    const Timestamp now = clock.periods.back().start + 15min;
    EXPECT_TRUE(isPlayedTime(clock, now, {0, std::nullopt}, false));
    EXPECT_TRUE(isPlayedTime(clock, now, {44, std::nullopt}, false));
    EXPECT_TRUE(isPlayedTime(clock, now, {45, 3}, false)); // 45+3' was played
    EXPECT_FALSE(isPlayedTime(clock, now, {45, 4}, false));
    EXPECT_TRUE(isPlayedTime(clock, now, {60, std::nullopt}, false));  // 61', now
    EXPECT_FALSE(isPlayedTime(clock, now, {61, std::nullopt}, false)); // the future
    EXPECT_FALSE(isPlayedTime(clock, now, {90, 1}, false));

    // The half-time break: the minute that starts the second half is a substitution only.
    const MatchClock inBreak = clockOf({2, 45}, {44min + 30s}, false);
    const Timestamp later = inBreak.periods.back().end.value() + 5min;
    EXPECT_TRUE(isPlayedTime(inBreak, later, {45, std::nullopt}, true));
    EXPECT_FALSE(isPlayedTime(inBreak, later, {45, std::nullopt}, false));
    EXPECT_TRUE(isPlayedTime(inBreak, later, {44, std::nullopt}, false)); // ended at 44:30
    EXPECT_FALSE(isPlayedTime(inBreak, later, {45, 1}, false));           // no stoppage time
    EXPECT_EQ(defaultTime(inBreak, later, true), (MatchTime{45, std::nullopt}));
    EXPECT_EQ(defaultTime(inBreak, later, false), (MatchTime{44, std::nullopt}));
    const MatchClock afterStoppage = clockOf({2, 45}, {47min + 10s}, false);
    EXPECT_EQ(defaultTime(afterStoppage, kKickoff + 1h, false), (MatchTime{45, 3}));
    // In stoppage time of a running part.
    const MatchClock stoppage = clockOf({3, 30}, {}, true);
    const Timestamp inStoppage = kKickoff + 31min + 10s;
    EXPECT_TRUE(isPlayedTime(stoppage, inStoppage, {29, std::nullopt}, false));
    EXPECT_TRUE(isPlayedTime(stoppage, inStoppage, {30, 2}, false));
    EXPECT_FALSE(isPlayedTime(stoppage, inStoppage, {30, 3}, false));
    EXPECT_FALSE(isPlayedTime(stoppage, inStoppage, {30, std::nullopt}, false)); // next part
    EXPECT_EQ(defaultTime(stoppage, inStoppage, false), (MatchTime{30, 2}));
}

TEST(MatchMinutesTest, SecondYellowIsCountedNotGuessedFromNeighbours) {
    // A yellow and a straight red at the same minute: a red card, not a second yellow.
    const std::vector straight{card(MatchEventType::Yellow, 30, 4),
                               card(MatchEventType::Red, 30, 4)};
    EXPECT_EQ(secondYellowReds(straight), (std::vector<bool>{false, false}));
    // Two yellows, then the red: a second yellow, whatever lies between and whatever the minutes.
    const std::vector second{card(MatchEventType::Yellow, 10, 4), goal(20, 9),
                             card(MatchEventType::Yellow, 50, 4), card(MatchEventType::Red, 50, 4)};
    EXPECT_EQ(secondYellowReds(second), (std::vector<bool>{false, false, false, true}));
    // The yellows of another player do not count.
    const std::vector other{card(MatchEventType::Yellow, 10, 5),
                            card(MatchEventType::Yellow, 20, 5), card(MatchEventType::Red, 30, 4)};
    EXPECT_EQ(secondYellowReds(other), (std::vector<bool>{false, false, false}));
    // Counted in replay order (by minute), not in the order given.
    const std::vector unordered{card(MatchEventType::Red, 60, 4),
                                card(MatchEventType::Yellow, 10, 4),
                                card(MatchEventType::Yellow, 50, 4)};
    EXPECT_EQ(secondYellowReds(unordered), (std::vector<bool>{true, false, false}));
}

TEST(MatchMinutesTest, AddedTimeComesBeforeTheNextPart) {
    // Entered late: a yellow at 45+2 after the half-time substitution at 45 (46').
    MatchEventDraft late = card(MatchEventType::Yellow, 45, 4);
    late.addedTime = 2;
    const std::vector events{sub(45, 12, 4), late};
    EXPECT_EQ(replayOrder(events), (std::vector<std::size_t>{1, 0}));
    // So the yellow is for a player still on the pitch.
    EXPECT_TRUE(replayMatch(sheet(), events).has_value());
}

TEST(MatchMinutesTest, AYellowEnteredLaterCannotMakeASecondOneWithoutItsRed) {
    // 4 got a yellow at 50 and a second one (with the red) at 70. A yellow entered later at 30
    // would make the one at 50 the second, without a red: refused at that card.
    const std::vector events{card(MatchEventType::Yellow, 50, 4),
                             card(MatchEventType::Yellow, 70, 4), card(MatchEventType::Red, 70, 4),
                             card(MatchEventType::Yellow, 30, 4)};
    const auto replayed = replayMatch(sheet(), events);
    ASSERT_FALSE(replayed.has_value());
    EXPECT_EQ(replayed.error(), (MatchEventError{MatchEventProblem::SecondYellowWithoutRed, 0}));
}

TEST(MatchMinutesTest, PlayersOnThePitchAreCounted) {
    EXPECT_EQ(onPitchCount(startingSituation(sheet())), 11);
    const std::vector events{card(MatchEventType::Red, 10, 2), card(MatchEventType::Red, 20, 3),
                             sub(30, 12, 5)};
    EXPECT_EQ(onPitchCount(replayMatch(sheet(), events).value()), 9);
    EXPECT_EQ(kMinPlayersOnPitch, 7);
}

// --- Set clock and the mode without the clock (P13 manual test) ---

TEST(MatchClockTest, ClockTextOfTheRunningPart) {
    const MatchFormat halves{2, 45};
    EXPECT_EQ(elapsedFromClockText("23:41", halves, 1), 23min + 41s);
    EXPECT_EQ(elapsedFromClockText("23", halves, 1), 23min);
    EXPECT_EQ(elapsedFromClockText(" 0:05 ", halves, 1), 5s);
    EXPECT_EQ(elapsedFromClockText("45+2:10", halves, 1), 47min + 10s);
    // The second half as its clock shows it: from 45:00.
    EXPECT_EQ(elapsedFromClockText("67:30", halves, 2), 22min + 30s);
    EXPECT_EQ(elapsedFromClockText("90+1:00", halves, 2), 46min);
    for (const char* bad : {"", "x", "23:6", "23:60", "44:00+1", "45+", "45+2:10", "30:00"}) {
        EXPECT_EQ(elapsedFromClockText(bad, halves, 2), std::nullopt) << bad;
    }
    EXPECT_EQ(elapsedFromClockText("46:00", halves, 1), std::nullopt); // over 45: write 45+1:00
    EXPECT_EQ(elapsedFromClockText("30+1:00", {3, 30}, 1), 31min);
    EXPECT_EQ(elapsedFromClockText("10:00", halves, 3), std::nullopt);
}

TEST(MatchClockTest, TheLeastClockKeepsTheLatestActionOfThePartPlayed) {
    const MatchFormat halves{2, 45};
    // The latest action of the running part decides; one of an earlier part needs nothing.
    EXPECT_EQ(minElapsedFor(halves, 1, MatchTime{30, std::nullopt}), 30min);
    EXPECT_EQ(minElapsedFor(halves, 2, MatchTime{52, std::nullopt}), 7min); // 52 - 45
    EXPECT_EQ(minElapsedFor(halves, 1, MatchTime{45, 3}), 47min);           // 45+3' needs 47:00
    EXPECT_EQ(minElapsedFor(halves, 2, MatchTime{45, 3}), 0s);
    EXPECT_EQ(minElapsedFor(halves, 2, MatchTime{30, std::nullopt}), 0s);
    EXPECT_EQ(minElapsedFor(halves, 1, std::nullopt), 0s);
}

TEST(MatchClockTest, WithoutTheClockAnyMinuteOfTheFormat) {
    MatchClock clock;
    clock.format = {2, 45};
    clock.clockless = true;
    EXPECT_EQ(phaseOf(clock), MatchPhase::Running);
    EXPECT_EQ(nextStep(clock), std::nullopt);
    EXPECT_EQ(readClock(clock, kKickoff).regular, 0s);
    for (const MatchTime time :
         {MatchTime{0, std::nullopt}, MatchTime{89, std::nullopt}, MatchTime{45, 2},
          MatchTime{90, 30}, MatchTime{45, std::nullopt}}) {
        EXPECT_TRUE(isPlayedTime(clock, kKickoff, time, false)) << time.minute;
    }
    for (const MatchTime time : {MatchTime{90, std::nullopt}, MatchTime{44, 1}, MatchTime{90, 31},
                                 MatchTime{135, 1}, MatchTime{-1, std::nullopt}}) {
        EXPECT_FALSE(isPlayedTime(clock, kKickoff, time, false)) << time.minute;
    }
    const std::vector events{goal(70, 9), card(MatchEventType::Red, 20, 4)};
    EXPECT_EQ(earlyEndMinute(clock, kKickoff, latestRecordedTime(events, {}, {})), 70);
    clock.finished = true;
    EXPECT_EQ(finalMinute(clock), 90);
    clock.endedEarlyAt = 70;
    EXPECT_EQ(finalMinute(clock), 70);
    EXPECT_EQ(readClock(clock, kKickoff).eventTime, (MatchTime{70, std::nullopt}));
}
