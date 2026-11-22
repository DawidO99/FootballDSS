#include "core/algorithms/Load.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <optional>
#include <vector>

using namespace fdss::core;
using namespace std::chrono;

namespace {

constexpr double kExact = 1e-9;
constexpr Date kDay0 = 2026y / November / 2d; // a Monday

Date onDay(int offset) { return Date{sys_days{kDay0} + days{offset}}; }

LoadEntry session(int offset, std::optional<int> rpe, int minutes) {
    return LoadEntry{onDay(offset), LoadSource::Session, rpe, minutes};
}

LoadEntry match(int offset, std::optional<int> rpe, int minutes) {
    return LoadEntry{onDay(offset), LoadSource::Match, rpe, minutes};
}

// Indicators that pass every data check, with the given ratio.
LoadIndicators withRatio(double ratio, double monotony = 1.0) {
    LoadIndicators indicators;
    indicators.rpeDays = 14;
    indicators.daysSinceFirstEntry = 28;
    indicators.acute = ratio * 100.0;
    indicators.chronic = 100.0;
    indicators.ratio = ratio;
    indicators.monotony = monotony;
    return indicators;
}

LoadStatus statusOf(const LoadIndicators& indicators) {
    return classifyLoad(indicators, LoadThresholds{}).status;
}

std::vector<LoadReason> reasonsOf(const LoadIndicators& indicators) {
    return classifyLoad(indicators, LoadThresholds{}).reasons;
}

PlayerAttendance attendance(AttendanceStatus status, std::optional<int> rpe,
                            std::optional<int> minutesOverride, int duration = 90) {
    PlayerAttendance row;
    row.attendance.status = status;
    row.attendance.rpe = rpe;
    row.attendance.minutesOverride = minutesOverride;
    row.date = kDay0;
    row.durationMin = duration;
    return row;
}

Appearance appearance(bool started, std::optional<int> on, std::optional<int> off,
                      std::optional<int> rpe = 7) {
    Appearance result;
    result.started = started;
    result.minuteOn = on;
    result.minuteOff = off;
    result.rpe = rpe;
    return result;
}

} // namespace

TEST(LoadTest, LambdaIsTwoOverNPlusOne) {
    EXPECT_DOUBLE_EQ(ewmaLambda(7), 0.25);
    EXPECT_DOUBLE_EQ(ewmaLambda(28), 2.0 / 29.0);
}

// Day 0: 10 × 10 min = 100 AU; day 1: rest; day 2: 5 × 40 min = 200 AU; day 3: rest.
// Acute (λ = 2/8 = 0.25), seeded with the first day:
//   a0 = 100
//   a1 = 0.25 × 0   + 0.75 × 100   = 75
//   a2 = 0.25 × 200 + 0.75 × 75    = 50 + 56.25 = 106.25
//   a3 = 0.25 × 0   + 0.75 × 106.25 = 79.6875
// Chronic (λ = 2/29):
//   c0 = 100
//   c1 = 27/29 × 100                     = 2700/29          ≈ 93.103448275862
//   c2 = 2/29 × 200 + 27/29 × 2700/29    = 84500/841        ≈ 100.475624256837
//   c3 = 27/29 × 84500/841               = 2281500/24389    ≈ 93.546270859814
// Ratio on day 3: 79.6875 / 93.546270859814 ≈ 0.851851166995
TEST(LoadTest, EwmaAndRatioMatchTheHandCalculation) {
    const std::vector<LoadEntry> entries{session(0, 10, 10), match(2, 5, 40)};
    const auto series = loadSeries(entries, onDay(3), LoadThresholds{});

    ASSERT_EQ(series.size(), 4U);
    EXPECT_NEAR(series[0].acute, 100.0, kExact);
    EXPECT_NEAR(series[1].acute, 75.0, kExact);
    EXPECT_NEAR(series[2].acute, 106.25, kExact);
    EXPECT_NEAR(series[3].acute, 79.6875, kExact);
    EXPECT_NEAR(series[0].chronic, 100.0, kExact);
    EXPECT_NEAR(series[1].chronic, 2700.0 / 29.0, kExact);
    EXPECT_NEAR(series[2].chronic, 84500.0 / 841.0, kExact);
    EXPECT_NEAR(series[3].chronic, 2281500.0 / 24389.0, kExact);
    ASSERT_TRUE(series[3].ratio().has_value());
    EXPECT_NEAR(*series[3].ratio(), 0.851851166995398, kExact);

    EXPECT_EQ(series[0].sessionLoad, 100);
    EXPECT_EQ(series[2].matchLoad, 200);
    EXPECT_EQ(series[2].sessionLoad, 0);
    EXPECT_EQ(series[1].total(), 0);
    EXPECT_TRUE(series[0].hasRpe);
    EXPECT_FALSE(series[1].hasRpe);
}

TEST(LoadTest, TheSeriesStartsOnTheFirstEntryAndEarlierDaysAreNotZeros) {
    const std::vector<LoadEntry> entries{session(5, 6, 50), session(7, 4, 50)};
    const auto series = loadSeries(entries, onDay(9), LoadThresholds{});

    ASSERT_EQ(series.size(), 5U);
    EXPECT_EQ(series.front().date, onDay(5));
    EXPECT_EQ(series.back().date, onDay(9));
    // Seeded with the load of the first day, not with zeros before it.
    EXPECT_DOUBLE_EQ(series.front().acute, 300.0);
    EXPECT_DOUBLE_EQ(series.front().chronic, 300.0);
    EXPECT_DOUBLE_EQ(*series.front().ratio(), 1.0);
}

TEST(LoadTest, EntriesWithoutRpeOrAfterTheEndAddNothing) {
    const std::vector<LoadEntry> entries{session(0, std::nullopt, 90), session(1, 5, 60),
                                         match(2, std::nullopt, 90), session(4, 9, 90)};
    const auto series = loadSeries(entries, onDay(3), LoadThresholds{});

    ASSERT_EQ(series.size(), 3U);
    EXPECT_EQ(series.front().date, onDay(1));
    EXPECT_EQ(series[1].total(), 0);
    EXPECT_TRUE(loadSeries(entries, onDay(0), LoadThresholds{}).empty());
    EXPECT_TRUE(loadSeries({}, onDay(3), LoadThresholds{}).empty());
}

TEST(LoadTest, SessionsAndMatchesOfOneDayAddUp) {
    const std::vector<LoadEntry> entries{session(0, 4, 60), session(0, 3, 30), match(0, 8, 90)};
    const auto series = loadSeries(entries, onDay(0), LoadThresholds{});

    ASSERT_EQ(series.size(), 1U);
    EXPECT_EQ(series[0].sessionLoad, (4 * 60) + (3 * 30));
    EXPECT_EQ(series[0].matchLoad, 8 * 90);
}

// Mean = 1170 / 7; n·Σx² − (Σx)² = 7 × 382500 − 1170² = 1308600, so SD = √1308600 / 7 and
// monotony = 1170 / √1308600 ≈ 1.022780416276; strain = 1170 × monotony ≈ 1196.653087043.
TEST(LoadTest, MonotonyIsMeanOverPopulationSd) {
    const std::vector<int> week{0, 300, 0, 450, 240, 0, 180};
    const auto monotony = loadMonotony(week);

    ASSERT_TRUE(monotony.has_value());
    EXPECT_NEAR(*monotony, 1.022780416275883, kExact);
}

TEST(LoadTest, MonotonyIsUndefinedWithoutSpread) {
    EXPECT_FALSE(loadMonotony(std::vector<int>(7, 300)).has_value());
    EXPECT_FALSE(loadMonotony(std::vector<int>(7, 0)).has_value());
    EXPECT_FALSE(loadMonotony({}).has_value());
}

TEST(LoadTest, StrainIsWeekLoadTimesMonotony) {
    // Mon..Sun as in MonotonyIsMeanOverPopulationSd, after 30 days of entries.
    std::vector<LoadEntry> entries{session(-30, 5, 60)};
    const std::vector<int> week{0, 300, 0, 450, 240, 0, 180};
    for (int index = 0; index < 7; ++index) {
        if (week[static_cast<std::size_t>(index)] > 0) {
            entries.push_back(session(index, 1, week[static_cast<std::size_t>(index)]));
        }
    }
    // An RPE entry today (day 6): the week ends today.
    const auto load = evaluateLoad(entries, onDay(6), LoadThresholds{});
    const LoadIndicators& indicators = load.assessment.indicators;

    EXPECT_EQ(load.assessment.asOf, onDay(6));
    EXPECT_EQ(indicators.weekLoad, 1170);
    ASSERT_TRUE(indicators.monotony.has_value());
    EXPECT_NEAR(*indicators.monotony, 1.022780416275883, kExact);
    ASSERT_TRUE(indicators.strain.has_value());
    EXPECT_NEAR(*indicators.strain, 1196.653087042783, 1e-6);
}

TEST(LoadTest, SessionEntryOnlyForPresentRows) {
    const auto withOverride = sessionLoadEntry(attendance(AttendanceStatus::Present, 6, 45, 90));
    ASSERT_TRUE(withOverride.has_value());
    EXPECT_EQ(withOverride->minutes, 45);
    EXPECT_EQ(withOverride->rpe, 6);
    EXPECT_EQ(withOverride->source, LoadSource::Session);
    EXPECT_EQ(withOverride->date, kDay0);

    const auto fullSession =
        sessionLoadEntry(attendance(AttendanceStatus::Present, 6, std::nullopt, 75));
    ASSERT_TRUE(fullSession.has_value());
    EXPECT_EQ(fullSession->minutes, 75);

    const auto missingRpe =
        sessionLoadEntry(attendance(AttendanceStatus::Present, std::nullopt, std::nullopt));
    ASSERT_TRUE(missingRpe.has_value());
    EXPECT_FALSE(missingRpe->rpe.has_value());

    for (const auto status :
         {AttendanceStatus::Absent, AttendanceStatus::Excused, AttendanceStatus::Injured}) {
        EXPECT_FALSE(sessionLoadEntry(attendance(status, std::nullopt, std::nullopt)))
            << toDbText(status);
    }
}

TEST(LoadTest, MatchLoadComesFromTheMinutesPlayed) {
    const auto starter = matchLoadEntry(appearance(true, std::nullopt, 90, 8), kDay0);
    ASSERT_TRUE(starter.has_value());
    EXPECT_EQ(starter->minutes, 90);
    EXPECT_EQ(starter->source, LoadSource::Match);
    // 8 × 90 = 720 AU on the match day.
    const auto series = loadSeries(std::vector<LoadEntry>{*starter}, kDay0, LoadThresholds{});
    ASSERT_EQ(series.size(), 1U);
    EXPECT_EQ(series[0].matchLoad, 720);

    EXPECT_EQ(matchLoadEntry(appearance(true, 0, 90), kDay0)->minutes, 90);
    EXPECT_EQ(matchLoadEntry(appearance(true, std::nullopt, 55), kDay0)->minutes, 55); // sent off
    EXPECT_EQ(matchLoadEntry(appearance(false, 60, 90), kDay0)->minutes, 30);
    EXPECT_FALSE(matchLoadEntry(appearance(false, 60, 90, std::nullopt), kDay0)->rpe);

    // Did not play: nothing, not even missing RPE.
    EXPECT_FALSE(matchLoadEntry(appearance(false, std::nullopt, std::nullopt), kDay0));
    EXPECT_FALSE(matchLoadEntry(appearance(false, std::nullopt, 90), kDay0));
    EXPECT_FALSE(matchLoadEntry(appearance(true, std::nullopt, std::nullopt), kDay0));
    EXPECT_FALSE(matchLoadEntry(appearance(false, 90, 90), kDay0));
}

TEST(LoadTest, RatioBoundaries) {
    EXPECT_EQ(statusOf(withRatio(1.0)), LoadStatus::Normal);
    EXPECT_EQ(statusOf(withRatio(0.8)), LoadStatus::Normal);
    EXPECT_EQ(statusOf(withRatio(std::nextafter(0.8, 0.0))), LoadStatus::Underloaded);
    EXPECT_EQ(statusOf(withRatio(1.3)), LoadStatus::Normal);
    EXPECT_EQ(statusOf(withRatio(std::nextafter(1.3, 2.0))), LoadStatus::Elevated);
    EXPECT_EQ(statusOf(withRatio(1.5)), LoadStatus::Elevated);
    EXPECT_EQ(statusOf(withRatio(std::nextafter(1.5, 2.0))), LoadStatus::High);

    EXPECT_EQ(reasonsOf(withRatio(1.0)), std::vector{LoadReason::RatioInRange});
    EXPECT_EQ(reasonsOf(withRatio(0.5)), std::vector{LoadReason::RatioLow});
    EXPECT_EQ(reasonsOf(withRatio(1.4)), std::vector{LoadReason::RatioElevated});
    EXPECT_EQ(reasonsOf(withRatio(1.6)), std::vector{LoadReason::RatioHigh});
}

TEST(LoadTest, MonotonyBoundaryAndPrecedence) {
    EXPECT_EQ(statusOf(withRatio(1.0, 2.0)), LoadStatus::Normal);
    EXPECT_EQ(statusOf(withRatio(1.0, std::nextafter(2.0, 3.0))), LoadStatus::Elevated);
    EXPECT_EQ(reasonsOf(withRatio(1.0, 2.5)), std::vector{LoadReason::MonotonyHigh});

    // Yellow beats underloaded and names both; red names the monotony too.
    EXPECT_EQ(statusOf(withRatio(0.5, 2.5)), LoadStatus::Elevated);
    EXPECT_EQ(reasonsOf(withRatio(0.5, 2.5)),
              (std::vector{LoadReason::MonotonyHigh, LoadReason::RatioLow}));
    EXPECT_EQ(reasonsOf(withRatio(1.4, 2.5)),
              (std::vector{LoadReason::RatioElevated, LoadReason::MonotonyHigh}));
    EXPECT_EQ(statusOf(withRatio(1.6, 2.5)), LoadStatus::High);
    EXPECT_EQ(reasonsOf(withRatio(1.6, 2.5)),
              (std::vector{LoadReason::RatioHigh, LoadReason::MonotonyHigh}));

    // Undefined monotony (SD = 0) is not "high".
    LoadIndicators flat = withRatio(1.0);
    flat.monotony.reset();
    EXPECT_EQ(statusOf(flat), LoadStatus::Normal);
}

TEST(LoadTest, NotEnoughDataBoundaries) {
    LoadIndicators indicators = withRatio(1.0);
    indicators.rpeDays = 13;
    EXPECT_EQ(statusOf(indicators), LoadStatus::NotEnoughData);
    EXPECT_EQ(reasonsOf(indicators), std::vector{LoadReason::FewRpeDays});
    indicators.rpeDays = 14;
    EXPECT_EQ(statusOf(indicators), LoadStatus::Normal);

    indicators.daysSinceFirstEntry = 27;
    EXPECT_EQ(statusOf(indicators), LoadStatus::NotEnoughData);
    EXPECT_EQ(reasonsOf(indicators), std::vector{LoadReason::ShortBaseline});
    indicators.daysSinceFirstEntry = 28;
    EXPECT_EQ(statusOf(indicators), LoadStatus::Normal);

    // Never a fake green, whatever the ratio says.
    indicators.rpeDays = 3;
    indicators.daysSinceFirstEntry = 4;
    EXPECT_EQ(classifyLoad(indicators, LoadThresholds{}),
              (LoadVerdict{LoadStatus::NotEnoughData,
                           {LoadReason::FewRpeDays, LoadReason::ShortBaseline}}));

    LoadIndicators noChronic = withRatio(1.0);
    noChronic.ratio.reset();
    EXPECT_EQ(reasonsOf(noChronic), std::vector{LoadReason::NoChronicLoad});
    EXPECT_EQ(reasonsOf(LoadIndicators{}), std::vector{LoadReason::NoRpe});
    EXPECT_EQ(statusOf(LoadIndicators{}), LoadStatus::NotEnoughData);
}

TEST(LoadTest, ThresholdsArePassedIn) {
    LoadThresholds strict;
    strict.ratioHigh = 1.2;
    strict.ratioElevated = 1.1;
    EXPECT_EQ(classifyLoad(withRatio(1.25), strict).status, LoadStatus::High);
    EXPECT_EQ(classifyLoad(withRatio(1.15), strict).status, LoadStatus::Elevated);
    EXPECT_EQ(statusOf(withRatio(1.25)), LoadStatus::Normal);
}

TEST(LoadTest, TheDayEndsTodayOnlyWithAnRpeEntryToday) {
    const std::vector<LoadEntry> withToday{session(0, 5, 60), session(3, 5, 60)};
    EXPECT_EQ(evaluateLoad(withToday, onDay(3), LoadThresholds{}).assessment.asOf, onDay(3));
    EXPECT_EQ(evaluateLoad(withToday, onDay(3), LoadThresholds{}).days.back().date, onDay(3));

    // Attendance saved for today, RPE still to come: as of yesterday.
    const std::vector<LoadEntry> pending{session(0, 5, 60), session(3, std::nullopt, 60)};
    const auto load = evaluateLoad(pending, onDay(3), LoadThresholds{});
    EXPECT_EQ(load.assessment.asOf, onDay(2));
    EXPECT_EQ(load.days.back().date, onDay(2));
    EXPECT_EQ(load.assessment.missingRpeSessions, 1);
}

TEST(LoadTest, FutureEntriesAreIgnored) {
    const std::vector<LoadEntry> entries{session(0, 5, 60), session(5, 9, 90), match(6, 9, 90)};
    const auto load = evaluateLoad(entries, onDay(3), LoadThresholds{});

    EXPECT_EQ(load.days.back().date, onDay(2));
    EXPECT_EQ(load.assessment.indicators.rpeDays, 1);
    EXPECT_EQ(load.assessment.indicators.weekLoad, 300);
}

TEST(LoadTest, ANewClubWithThreeDaysOfRpeIsGreyWithAHint) {
    const std::vector<LoadEntry> entries{session(0, 5, 90), session(2, 7, 90), session(3, 4, 60)};
    const auto load = evaluateLoad(entries, onDay(3), LoadThresholds{});
    const LoadAssessment& assessment = load.assessment;

    EXPECT_EQ(assessment.verdict.status, LoadStatus::NotEnoughData);
    EXPECT_EQ(assessment.verdict.reasons,
              (std::vector{LoadReason::FewRpeDays, LoadReason::ShortBaseline}));
    EXPECT_EQ(assessment.indicators.rpeDays, 3);
    EXPECT_EQ(assessment.rpeDaysMissing, 11);
    EXPECT_EQ(assessment.firstEntry, onDay(0));
    EXPECT_EQ(assessment.baselineFrom, onDay(28));
    // The acute load is there to show.
    EXPECT_GT(assessment.indicators.acute, 0.0);
}

TEST(LoadTest, FourteenRpeDaysAndTwentyEightDaysGiveAStatus) {
    // Every second day from day 0 to day 26 (14 days), then rest through day 28.
    std::vector<LoadEntry> entries;
    for (int offset = 0; offset <= 26; offset += 2) {
        entries.push_back(session(offset, 5, 60));
    }
    // Today = day 28 with an entry: 28 days since the first one.
    entries.push_back(session(28, 5, 60));
    const auto ready = evaluateLoad(entries, onDay(28), LoadThresholds{});
    EXPECT_EQ(ready.assessment.indicators.rpeDays, 15);
    EXPECT_EQ(ready.assessment.indicators.daysSinceFirstEntry, 28);
    EXPECT_NE(ready.assessment.verdict.status, LoadStatus::NotEnoughData);

    // Day 27 has no entry, so "today = day 28" without today's entry is as of day 27.
    entries.pop_back();
    const auto oneDayShort = evaluateLoad(entries, onDay(28), LoadThresholds{});
    EXPECT_EQ(oneDayShort.assessment.indicators.rpeDays, 14);
    EXPECT_EQ(oneDayShort.assessment.indicators.daysSinceFirstEntry, 27);
    EXPECT_EQ(oneDayShort.assessment.verdict.reasons, std::vector{LoadReason::ShortBaseline});

    // 13 RPE days, but long enough.
    entries.erase(entries.begin() + 1);
    const auto fewDays = evaluateLoad(entries, onDay(29), LoadThresholds{});
    EXPECT_EQ(fewDays.assessment.indicators.rpeDays, 13);
    EXPECT_EQ(fewDays.assessment.rpeDaysMissing, 1);
    EXPECT_EQ(fewDays.assessment.verdict.reasons, std::vector{LoadReason::FewRpeDays});
}

TEST(LoadTest, ASessionAndAMatchOnOneDayAreOneRpeDay) {
    const std::vector<LoadEntry> entries{session(0, 5, 60), match(0, 8, 90), session(1, 0, 60)};
    const auto load = evaluateLoad(entries, onDay(1), LoadThresholds{});
    // RPE 0 is an RPE.
    EXPECT_EQ(load.assessment.indicators.rpeDays, 2);
}

TEST(LoadTest, MissingRpeIsCountedInTheLastTwentyEightDays) {
    const std::vector<LoadEntry> entries{session(0, std::nullopt, 60), // 28 days before today
                                         session(1, std::nullopt, 60), // 27 days before
                                         match(10, std::nullopt, 90), session(20, 5, 60),
                                         session(28, std::nullopt, 60)}; // today
    const auto load = evaluateLoad(entries, onDay(28), LoadThresholds{});

    EXPECT_EQ(load.assessment.missingRpeSessions, 2);
    EXPECT_EQ(load.assessment.missingRpeMatches, 1);
}

// 60 days of 300 AU, a break of 21 days, then 7 days of 300 AU again: the chronic load fell during
// the break, so the ratio is high, and the explanation says why.
TEST(LoadTest, AHighRatioAfterABreakSaysSo) {
    std::vector<LoadEntry> entries;
    for (int offset = 0; offset < 60; ++offset) {
        entries.push_back(session(offset, 5, 60));
    }
    for (int offset = 81; offset < 88; ++offset) {
        entries.push_back(session(offset, 5, 60));
    }
    const auto load = evaluateLoad(entries, onDay(87), LoadThresholds{});
    const LoadAssessment& assessment = load.assessment;

    EXPECT_EQ(assessment.indicators.breakDays, 21);
    EXPECT_EQ(assessment.verdict.status, LoadStatus::High);
    EXPECT_EQ(assessment.verdict.reasons,
              (std::vector{LoadReason::RatioHigh, LoadReason::AfterBreak}));

    // The same break long ago is no longer "after a break".
    for (int offset = 88; offset < 130; ++offset) {
        entries.push_back(session(offset, 5, 60));
    }
    const auto later = evaluateLoad(entries, onDay(129), LoadThresholds{});
    EXPECT_FALSE(later.assessment.indicators.breakDays.has_value());
    EXPECT_EQ(later.assessment.verdict.status, LoadStatus::Normal);
}

TEST(LoadTest, TheDefaultThresholdsAreValid) { EXPECT_TRUE(validate(LoadThresholds{}).empty()); }

TEST(LoadTest, ThresholdsOutOfOrderOrRangeNameTheirField) {
    const auto fields = [](const LoadThresholds& thresholds) {
        std::vector<std::string> names;
        for (const ValidationError& error : validate(thresholds)) {
            names.push_back(error.field);
        }
        return names;
    };
    LoadThresholds thresholds;
    thresholds.ratioLow = 0.0;
    EXPECT_EQ(fields(thresholds), std::vector<std::string>{"ratio_low"});

    thresholds = {};
    thresholds.ratioElevated = 0.8; // equal to the underload ratio
    EXPECT_EQ(fields(thresholds), std::vector<std::string>{"ratio_elevated"});

    thresholds = {};
    thresholds.ratioHigh = 1.3; // equal to the elevated ratio
    EXPECT_EQ(fields(thresholds), std::vector<std::string>{"ratio_high"});

    thresholds = {};
    thresholds.ratioHigh = kMaxRatio;
    EXPECT_TRUE(fields(thresholds).empty());
    thresholds.ratioHigh = kMaxRatio + 0.01;
    EXPECT_EQ(fields(thresholds), std::vector<std::string>{"ratio_high"});

    thresholds = {};
    thresholds.monotonyHigh = 1.0;
    EXPECT_EQ(fields(thresholds), std::vector<std::string>{"monotony_high"});
    thresholds.monotonyHigh = kMaxMonotony;
    EXPECT_TRUE(fields(thresholds).empty());

    thresholds = {};
    thresholds.ratioLow = std::nan("");
    thresholds.monotonyHigh = std::nan("");
    EXPECT_EQ(fields(thresholds),
              (std::vector<std::string>{"ratio_low", "ratio_elevated", "monotony_high"}));
}

// P19 review: at most two decimals, the precision the screens show.
TEST(LoadTest, AThresholdWithMoreThanTwoDecimalsIsRefused) {
    LoadThresholds thresholds;
    thresholds.ratioHigh = 1.501;
    ASSERT_EQ(validate(thresholds).size(), 1U);
    EXPECT_EQ(validate(thresholds).front().field, "ratio_high");
    thresholds.ratioHigh = 1.75;
    EXPECT_TRUE(validate(thresholds).empty());
    thresholds.monotonyHigh = 2.125;
    ASSERT_EQ(validate(thresholds).size(), 1U);
    EXPECT_EQ(validate(thresholds).front().field, "monotony_high");
    EXPECT_TRUE(hasThresholdDecimals(0.07));
    EXPECT_TRUE(hasThresholdDecimals(1.1));
    EXPECT_FALSE(hasThresholdDecimals(0.005));
}
