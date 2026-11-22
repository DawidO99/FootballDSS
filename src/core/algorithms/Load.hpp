#pragma once

#include "core/domain/Appearance.hpp"
#include "core/domain/Attendance.hpp"
#include "core/domain/DateTypes.hpp"
#include "core/domain/Validation.hpp"

#include <optional>
#include <span>
#include <vector>

// Load monitoring (SPEC section 7.1, packet P11): session RPE (Foster 2001), daily load, acute and
// chronic load as EWMA (Williams et al. 2017), their ratio, monotony and strain (Foster 1998), and
// a status. The status is a signal to talk to the player, never a diagnosis. Pure: callers pass
// "today" and the thresholds in.

namespace fdss::core {

enum class LoadSource { Session, Match };

// One training session or match of one player. Without an RPE it adds nothing and is counted as
// missing RPE.
struct LoadEntry {
    Date date{};
    LoadSource source = LoadSource::Session;
    std::optional<int> rpe;
    int minutes = 0;

    friend bool operator==(const LoadEntry&, const LoadEntry&) = default;
};

// Only a present row: minutes = minutes_override, otherwise the duration of the session.
[[nodiscard]] std::optional<LoadEntry> sessionLoadEntry(const PlayerAttendance& row) noexcept;
// Minutes = minute_off − minute_on, a starter from 0. Without minute_off, a substitute without
// minute_on or no minutes at all, the player did not play: nullopt.
[[nodiscard]] std::optional<LoadEntry> matchLoadEntry(const Appearance& appearance,
                                                      Date matchDate) noexcept;

// The defaults of SPEC section 7.1. Passed in everywhere; the four ratios and monotony are
// editable in Settings (P19), the windows of days are not.
struct LoadThresholds {
    double ratioHigh = 1.5;     // above: High (red)
    double ratioElevated = 1.3; // above: Elevated (yellow)
    double ratioLow = 0.8;      // below: Underloaded
    double monotonyHigh = 2.0;  // above: Elevated
    int minRpeDays = 14;        // days with RPE before there is a status
    int baselineDays = 28;      // days since the first entry before there is a status
    int acuteDays = 7;          // N of the acute EWMA; also the window of monotony and strain
    int chronicDays = 28;       // N of the chronic EWMA; also the window of missing RPE and breaks
    int breakDays = 14;         // this many days without load in a row are a break

    friend bool operator==(const LoadThresholds&, const LoadThresholds&) = default;
};

// Limits of the editable thresholds (P19): 0 < ratioLow < ratioElevated < ratioHigh <= kMaxRatio,
// 1 < monotonyHigh <= kMaxMonotony, each with at most kThresholdDecimals decimals.
inline constexpr int kThresholdDecimals = 2;
inline constexpr double kMaxRatio = 5.0;
inline constexpr double kMaxMonotony = 10.0;

// One error per field that breaks a limit: "ratio_low", "ratio_elevated", "ratio_high",
// "monotony_high". An order that is wrong is the error of the higher one.
[[nodiscard]] ValidationErrors validate(const LoadThresholds& thresholds);
// True for a finite value with at most kThresholdDecimals decimals (1.5, 1.75; not 1.501).
[[nodiscard]] bool hasThresholdDecimals(double value);
// The value as the screens show it, with kThresholdDecimals decimals (1.504 → 1.5). classifyLoad()
// compares this, so what is seen decides (P19 review).
[[nodiscard]] double shownPrecision(double value);

// λ = 2 / (N + 1).
[[nodiscard]] constexpr double ewmaLambda(int days) noexcept { return 2.0 / (days + 1); }

struct LoadDay {
    Date date{};
    int sessionLoad = 0; // AU (RPE × minutes)
    int matchLoad = 0;   // AU
    double acute = 0.0;
    double chronic = 0.0;
    bool hasRpe = false; // at least one RPE entry, also RPE 0

    [[nodiscard]] int total() const noexcept { return sessionLoad + matchLoad; }
    // Empty while the chronic load is 0.
    [[nodiscard]] std::optional<double> ratio() const noexcept;

    friend bool operator==(const LoadDay&, const LoadDay&) = default;
};

// Every day from the first day with an RPE entry through `through`; days without data count as 0
// from then on, days before it are not part of the series. Both EWMAs start at the load of that
// first day. Empty without an RPE entry up to `through`; entries after it are ignored.
[[nodiscard]] std::vector<LoadDay> loadSeries(std::span<const LoadEntry> entries, Date through,
                                              const LoadThresholds& thresholds);

// Mean / standard deviation of the daily loads (population SD: the days are the whole window, not
// a sample). Empty input or SD = 0 (every day the same, also all zero): undefined.
[[nodiscard]] std::optional<double> loadMonotony(std::span<const int> dailyLoads);

enum class LoadStatus { NotEnoughData, Underloaded, Normal, Elevated, High };

// What drove a status (AGENTS section 9).
enum class LoadReason {
    NoRpe,         // not a single RPE entry
    FewRpeDays,    // RPE on fewer than minRpeDays days
    ShortBaseline, // fewer than baselineDays days since the first entry
    NoChronicLoad, // chronic load 0: there is no ratio
    RatioHigh,
    RatioElevated,
    MonotonyHigh,
    RatioLow,
    RatioInRange,
    AfterBreak, // the ratio is high because the chronic load is low after a break
};

struct LoadIndicators {
    int rpeDays = 0;
    // Empty without an RPE entry.
    std::optional<int> daysSinceFirstEntry;
    double acute = 0.0;
    double chronic = 0.0;
    std::optional<double> ratio;
    // The last acuteDays days; empty while the series is shorter.
    std::optional<double> monotony;
    std::optional<double> strain;
    int weekLoad = 0; // the last acuteDays days
    // The longest run of at least breakDays days without load that reached into the last
    // chronicDays days.
    std::optional<int> breakDays;

    friend bool operator==(const LoadIndicators&, const LoadIndicators&) = default;
};

struct LoadVerdict {
    LoadStatus status = LoadStatus::NotEnoughData;
    // Every reason that applies, the deciding one first.
    std::vector<LoadReason> reasons;

    friend bool operator==(const LoadVerdict&, const LoadVerdict&) = default;
};

// Green 0.8 <= ratio <= 1.3; yellow 1.3 < ratio <= 1.5 or monotony > 2; red ratio > 1.5;
// underloaded ratio < 0.8 (with the default thresholds). Red > yellow > underloaded > green.
// Not enough data while RPE was entered on fewer than minRpeDays days, fewer than baselineDays days
// passed since the first entry, or the chronic load is 0: never a fake green.
[[nodiscard]] LoadVerdict classifyLoad(const LoadIndicators& indicators,
                                       const LoadThresholds& thresholds);

struct LoadAssessment {
    LoadVerdict verdict;
    LoadIndicators indicators;
    // Today when the player has an RPE entry today, otherwise yesterday: the day is not over.
    Date asOf{};
    std::optional<Date> firstEntry;
    int rpeDaysMissing = 0;           // until minRpeDays, at least 0
    std::optional<Date> baselineFrom; // the first day with a full comparison
    // Present sessions and played matches without RPE in the last chronicDays days through today.
    int missingRpeSessions = 0;
    int missingRpeMatches = 0;

    friend bool operator==(const LoadAssessment&, const LoadAssessment&) = default;
};

struct PlayerLoad {
    std::vector<LoadDay> days; // through assessment.asOf
    LoadAssessment assessment;

    friend bool operator==(const PlayerLoad&, const PlayerLoad&) = default;
};

// `entries` belong to one player, in any order; those after today are ignored.
[[nodiscard]] PlayerLoad evaluateLoad(std::span<const LoadEntry> entries, Date today,
                                      const LoadThresholds& thresholds);

} // namespace fdss::core
