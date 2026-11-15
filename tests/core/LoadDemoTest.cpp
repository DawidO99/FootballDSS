#include "core/algorithms/Load.hpp"
#include "core/demo/DemoGenerator.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <format>
#include <iostream>
#include <string>
#include <vector>

// The load patterns of the demo club (P10 decision 7, P11 decision 2) with the formulas of SPEC
// section 7.1. The EWMA ratio follows the weekly microcycle (P11 decision 3), so every check runs
// for 7 consecutive "todays", Monday to Sunday. The generator depends on the date only through the
// weekday of today, so these 7 datasets are every demo club the app can create.

namespace {

using namespace fdss::core;
using namespace fdss::core::demo;
using std::chrono::days;
using std::chrono::sys_days;

constexpr Date kFirstToday{std::chrono::year{2026}, std::chrono::November, std::chrono::day{16}};
constexpr int kWeekdays = 7;
constexpr std::array<const char*, kWeekdays> kNames{"Mon", "Tue", "Wed", "Thu",
                                                    "Fri", "Sat", "Sun"};

Date addDays(Date date, int count) { return Date{sys_days{date} + days{count}}; }

std::vector<LoadEntry> entriesOf(const DemoDataset& data, int player) {
    std::vector<LoadEntry> entries;
    for (const DemoSession& session : data.sessions) {
        for (const DemoAttendance& demo : session.attendance) {
            if (demo.player != player) {
                continue;
            }
            PlayerAttendance row;
            row.attendance.status = demo.status;
            row.attendance.rpe = demo.rpe;
            row.date = session.draft.date;
            row.durationMin = session.draft.durationMin;
            if (const auto entry = sessionLoadEntry(row)) {
                entries.push_back(*entry);
            }
        }
    }
    for (const DemoMatch& match : data.matches) {
        if (match.status != MatchStatus::Finished) {
            continue;
        }
        for (const DemoAppearance& demo : match.appearances) {
            if (demo.player != player) {
                continue;
            }
            Appearance appearance;
            appearance.started = demo.started;
            appearance.minuteOn = demo.minuteOn;
            appearance.minuteOff = demo.minuteOff;
            appearance.rpe = demo.rpe;
            if (const auto entry = matchLoadEntry(appearance, match.draft.date)) {
                entries.push_back(*entry);
            }
        }
    }
    return entries;
}

struct DemoWeek {
    DemoStory story;
    // [weekday][active player]
    std::array<std::vector<LoadAssessment>, kWeekdays> assessments;
};

DemoWeek evaluateWeek() {
    DemoWeek week;
    for (int weekday = 0; weekday < kWeekdays; ++weekday) {
        const Date today = addDays(kFirstToday, weekday);
        const DemoDataset data = generate(kDemoSeed, addDays(today, -kDemoDays));
        week.story = data.story;
        for (int player = 0; player < kActivePlayers; ++player) {
            week.assessments[static_cast<std::size_t>(weekday)].push_back(
                evaluateLoad(entriesOf(data, player), today, LoadThresholds{}).assessment);
        }
    }
    return week;
}

std::string describe(const LoadAssessment& assessment) {
    return std::format("status {} ratio {:.2f} acute {:.0f} chronic {:.0f}",
                       static_cast<int>(assessment.verdict.status),
                       assessment.indicators.ratio.value_or(0.0), assessment.indicators.acute,
                       assessment.indicators.chronic);
}

class LoadDemoTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { s_week = evaluateWeek(); }

    static const LoadAssessment& at(int weekday, int player) {
        return s_week
            .assessments[static_cast<std::size_t>(weekday)][static_cast<std::size_t>(player)];
    }

    static DemoWeek s_week;
};

DemoWeek LoadDemoTest::s_week;

} // namespace

TEST_F(LoadDemoTest, TheStoryIsTheSameEveryDay) {
    // The stories are picked before the calendar, so they do not depend on the weekday.
    for (int weekday = 0; weekday < kWeekdays; ++weekday) {
        const Date today = addDays(kFirstToday, weekday);
        const DemoDataset data = generate(kDemoSeed, addDays(today, -kDemoDays));
        EXPECT_EQ(data.story.loadSpike, s_week.story.loadSpike) << kNames[weekday];
        EXPECT_EQ(data.story.returned, s_week.story.returned) << kNames[weekday];
    }
}

TEST_F(LoadDemoTest, TheLoadSpikeIsRedOnMostDaysAndNeverBelowYellow) {
    int redDays = 0;
    std::string distribution;
    for (int weekday = 0; weekday < kWeekdays; ++weekday) {
        const LoadAssessment& spike = at(weekday, s_week.story.loadSpike);
        distribution +=
            std::format("{} {:.2f}  ", kNames[weekday], spike.indicators.ratio.value_or(0.0));
        if (spike.verdict.status == LoadStatus::High) {
            ++redDays;
        }
        EXPECT_GE(spike.verdict.status, LoadStatus::Elevated)
            << kNames[weekday] << ": " << describe(spike);
    }
    EXPECT_GE(redDays, 5) << distribution;
    std::cout << "[ loadSpike ] " << distribution << "\n";
}

TEST_F(LoadDemoTest, NobodyElseIsRed) {
    for (int weekday = 0; weekday < kWeekdays; ++weekday) {
        for (int player = 0; player < kActivePlayers; ++player) {
            if (player == s_week.story.loadSpike || player == s_week.story.returned) {
                continue;
            }
            const LoadAssessment& assessment = at(weekday, player);
            EXPECT_NE(assessment.verdict.status, LoadStatus::High)
                << kNames[weekday] << ", player " << player << ": " << describe(assessment);
        }
    }
}

TEST_F(LoadDemoTest, EveryActivePlayerHasAFullBaseline) {
    // 12 weeks of data: nobody is grey for lack of data, so the screen shows real statuses.
    for (int weekday = 0; weekday < kWeekdays; ++weekday) {
        for (int player = 0; player < kActivePlayers; ++player) {
            const LoadAssessment& assessment = at(weekday, player);
            EXPECT_NE(assessment.verdict.status, LoadStatus::NotEnoughData)
                << kNames[weekday] << ", player " << player << ": " << describe(assessment);
        }
    }
}

TEST_F(LoadDemoTest, AHighRatioOfTheReturnedPlayerIsExplainedByTheBreak) {
    for (int weekday = 0; weekday < kWeekdays; ++weekday) {
        const LoadAssessment& returned = at(weekday, s_week.story.returned);
        if (returned.verdict.status == LoadStatus::High ||
            (returned.verdict.status == LoadStatus::Elevated &&
             returned.indicators.ratio > LoadThresholds{}.ratioElevated)) {
            EXPECT_NE(std::ranges::find(returned.verdict.reasons, LoadReason::AfterBreak),
                      returned.verdict.reasons.end())
                << kNames[weekday] << ": " << describe(returned);
        }
    }
}

TEST_F(LoadDemoTest, TheInjuredPlayersAreUnderloaded) {
    for (int weekday = 0; weekday < kWeekdays; ++weekday) {
        EXPECT_EQ(at(weekday, s_week.story.longInjury).verdict.status, LoadStatus::Underloaded)
            << kNames[weekday];
        EXPECT_EQ(at(weekday, s_week.story.returningSoon).verdict.status, LoadStatus::Underloaded)
            << kNames[weekday];
    }
}
