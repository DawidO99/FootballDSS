#include "core/algorithms/Load.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <numeric>

namespace fdss::core {

namespace {

using std::chrono::days;
using std::chrono::sys_days;

int daysBetween(Date from, Date to) {
    return static_cast<int>((sys_days{to} - sys_days{from}).count());
}

Date addDays(Date date, int count) { return Date{sys_days{date} + days{count}}; }

// The longest run of days without load that is at least thresholds.breakDays long and reaches into
// the last thresholds.chronicDays days of the series.
std::optional<int> longestBreak(std::span<const LoadDay> series, const LoadThresholds& thresholds) {
    const auto size = static_cast<int>(series.size());
    const int windowStart = size - thresholds.chronicDays;
    std::optional<int> longest;
    int runStart = -1;
    for (int index = 0; index <= size; ++index) {
        const bool rest = index < size && series[static_cast<std::size_t>(index)].total() == 0;
        if (rest) {
            if (runStart < 0) {
                runStart = index;
            }
            continue;
        }
        if (runStart >= 0) {
            const int length = index - runStart;
            const int runEnd = index - 1;
            if (length >= thresholds.breakDays && runEnd >= windowStart &&
                (!longest || length > *longest)) {
                longest = length;
            }
            runStart = -1;
        }
    }
    return longest;
}

} // namespace

std::optional<LoadEntry> sessionLoadEntry(const PlayerAttendance& row) noexcept {
    if (row.attendance.status != AttendanceStatus::Present) {
        return std::nullopt;
    }
    return LoadEntry{row.date, LoadSource::Session, row.attendance.rpe,
                     row.attendance.minutesOverride.value_or(row.durationMin)};
}

std::optional<LoadEntry> matchLoadEntry(const Appearance& appearance, Date matchDate) noexcept {
    if (!appearance.minuteOff || (!appearance.started && !appearance.minuteOn)) {
        return std::nullopt;
    }
    const int minuteOn = appearance.started ? 0 : *appearance.minuteOn;
    const int minutes = *appearance.minuteOff - minuteOn;
    if (minutes <= 0) {
        return std::nullopt;
    }
    return LoadEntry{matchDate, LoadSource::Match, appearance.rpe, minutes};
}

std::optional<double> LoadDay::ratio() const noexcept {
    if (chronic <= 0.0) {
        return std::nullopt;
    }
    return acute / chronic;
}

std::vector<LoadDay> loadSeries(std::span<const LoadEntry> entries, Date through,
                                const LoadThresholds& thresholds) {
    std::optional<Date> first;
    for (const LoadEntry& entry : entries) {
        if (entry.rpe && entry.date <= through && (!first || entry.date < *first)) {
            first = entry.date;
        }
    }
    if (!first) {
        return {};
    }

    std::vector<LoadDay> series(static_cast<std::size_t>(daysBetween(*first, through) + 1));
    for (std::size_t index = 0; index < series.size(); ++index) {
        series[index].date = addDays(*first, static_cast<int>(index));
    }
    for (const LoadEntry& entry : entries) {
        if (!entry.rpe || entry.date > through) {
            continue;
        }
        LoadDay& day = series[static_cast<std::size_t>(daysBetween(*first, entry.date))];
        const int load = *entry.rpe * entry.minutes;
        day.hasRpe = true;
        if (entry.source == LoadSource::Session) {
            day.sessionLoad += load;
        } else {
            day.matchLoad += load;
        }
    }

    const double acuteLambda = ewmaLambda(thresholds.acuteDays);
    const double chronicLambda = ewmaLambda(thresholds.chronicDays);
    series.front().acute = series.front().total();
    series.front().chronic = series.front().total();
    for (std::size_t index = 1; index < series.size(); ++index) {
        const double load = series[index].total();
        series[index].acute = acuteLambda * load + (1.0 - acuteLambda) * series[index - 1].acute;
        series[index].chronic =
            chronicLambda * load + (1.0 - chronicLambda) * series[index - 1].chronic;
    }
    return series;
}

std::optional<double> loadMonotony(std::span<const int> dailyLoads) {
    if (dailyLoads.empty()) {
        return std::nullopt;
    }
    // n² × variance in integers, so "every day the same" is exact.
    const auto count = static_cast<std::int64_t>(dailyLoads.size());
    std::int64_t sum = 0;
    std::int64_t sumOfSquares = 0;
    for (const int load : dailyLoads) {
        sum += load;
        sumOfSquares += static_cast<std::int64_t>(load) * load;
    }
    const std::int64_t spread = (count * sumOfSquares) - (sum * sum);
    if (spread <= 0) {
        return std::nullopt;
    }
    const double mean = static_cast<double>(sum) / static_cast<double>(count);
    const double deviation = std::sqrt(static_cast<double>(spread)) / static_cast<double>(count);
    return mean / deviation;
}

bool hasThresholdDecimals(double value) {
    const double hundredths = value * 100.0;
    return std::isfinite(hundredths) && std::abs(hundredths - std::round(hundredths)) < 1e-6;
}

double shownPrecision(double value) {
    constexpr double kScale = 100.0; // 10^kThresholdDecimals
    static_assert(kThresholdDecimals == 2);
    return std::round(value * kScale) / kScale;
}

ValidationErrors validate(const LoadThresholds& thresholds) {
    ValidationErrors errors;
    const auto invalid = [&](const char* field) { errors.push_back(ValidationError{field}); };
    // NaN fails every comparison, so it is refused too.
    if (!(thresholds.ratioLow > 0.0) || !(thresholds.ratioLow < kMaxRatio) ||
        !hasThresholdDecimals(thresholds.ratioLow)) {
        invalid("ratio_low");
    }
    if (!(thresholds.ratioElevated > thresholds.ratioLow) ||
        !(thresholds.ratioElevated < kMaxRatio) ||
        !hasThresholdDecimals(thresholds.ratioElevated)) {
        invalid("ratio_elevated");
    }
    if (!(thresholds.ratioHigh > thresholds.ratioElevated) ||
        !(thresholds.ratioHigh <= kMaxRatio) || !hasThresholdDecimals(thresholds.ratioHigh)) {
        invalid("ratio_high");
    }
    if (!(thresholds.monotonyHigh > 1.0) || !(thresholds.monotonyHigh <= kMaxMonotony) ||
        !hasThresholdDecimals(thresholds.monotonyHigh)) {
        invalid("monotony_high");
    }
    return errors;
}

LoadVerdict classifyLoad(const LoadIndicators& indicators, const LoadThresholds& thresholds) {
    LoadVerdict verdict;
    if (!indicators.daysSinceFirstEntry) {
        verdict.reasons.push_back(LoadReason::NoRpe);
        return verdict;
    }
    if (indicators.rpeDays < thresholds.minRpeDays) {
        verdict.reasons.push_back(LoadReason::FewRpeDays);
    }
    if (*indicators.daysSinceFirstEntry < thresholds.baselineDays) {
        verdict.reasons.push_back(LoadReason::ShortBaseline);
    }
    if (!indicators.ratio) {
        verdict.reasons.push_back(LoadReason::NoChronicLoad);
    }
    if (!verdict.reasons.empty()) {
        return verdict;
    }

    // As shown, with two decimals: a ratio of 1.504 reads 1.50 and is not above a threshold
    // of 1.50.
    const double ratio = shownPrecision(*indicators.ratio);
    const bool monotonyHigh =
        indicators.monotony && shownPrecision(*indicators.monotony) > thresholds.monotonyHigh;
    const bool afterBreak = indicators.breakDays.has_value();
    auto& reasons = verdict.reasons;
    if (ratio > thresholds.ratioHigh) {
        verdict.status = LoadStatus::High;
        reasons.push_back(LoadReason::RatioHigh);
        if (afterBreak) {
            reasons.push_back(LoadReason::AfterBreak);
        }
        if (monotonyHigh) {
            reasons.push_back(LoadReason::MonotonyHigh);
        }
    } else if (ratio > thresholds.ratioElevated || monotonyHigh) {
        verdict.status = LoadStatus::Elevated;
        if (ratio > thresholds.ratioElevated) {
            reasons.push_back(LoadReason::RatioElevated);
            if (afterBreak) {
                reasons.push_back(LoadReason::AfterBreak);
            }
        }
        if (monotonyHigh) {
            reasons.push_back(LoadReason::MonotonyHigh);
        }
        if (ratio < thresholds.ratioLow) {
            reasons.push_back(LoadReason::RatioLow);
        }
    } else if (ratio < thresholds.ratioLow) {
        verdict.status = LoadStatus::Underloaded;
        reasons.push_back(LoadReason::RatioLow);
    } else {
        verdict.status = LoadStatus::Normal;
        reasons.push_back(LoadReason::RatioInRange);
    }
    return verdict;
}

PlayerLoad evaluateLoad(std::span<const LoadEntry> entries, Date today,
                        const LoadThresholds& thresholds) {
    PlayerLoad result;
    LoadAssessment& assessment = result.assessment;
    LoadIndicators& indicators = assessment.indicators;

    const bool rpeToday = std::ranges::any_of(
        entries, [today](const LoadEntry& entry) { return entry.rpe && entry.date == today; });
    assessment.asOf = rpeToday ? today : addDays(today, -1);
    result.days = loadSeries(entries, assessment.asOf, thresholds);

    const Date missingFrom = addDays(today, -(thresholds.chronicDays - 1));
    std::vector<Date> rpeDates;
    for (const LoadEntry& entry : entries) {
        if (!entry.rpe && entry.date >= missingFrom && entry.date <= today) {
            ++(entry.source == LoadSource::Session ? assessment.missingRpeSessions
                                                   : assessment.missingRpeMatches);
        }
        if (entry.rpe && entry.date <= assessment.asOf) {
            rpeDates.push_back(entry.date);
        }
    }
    std::ranges::sort(rpeDates);
    const auto [last, end] = std::ranges::unique(rpeDates);
    rpeDates.erase(last, end);
    indicators.rpeDays = static_cast<int>(rpeDates.size());
    assessment.rpeDaysMissing = std::max(0, thresholds.minRpeDays - indicators.rpeDays);

    if (!result.days.empty()) {
        const std::vector<LoadDay>& series = result.days;
        assessment.firstEntry = series.front().date;
        assessment.baselineFrom = addDays(series.front().date, thresholds.baselineDays);
        indicators.daysSinceFirstEntry = static_cast<int>(series.size()) - 1;
        indicators.acute = series.back().acute;
        indicators.chronic = series.back().chronic;
        indicators.ratio = series.back().ratio();

        const auto window = std::min(series.size(), static_cast<std::size_t>(thresholds.acuteDays));
        std::vector<int> week;
        std::ranges::transform(
            std::ranges::subrange(series.end() - static_cast<std::ptrdiff_t>(window), series.end()),
            std::back_inserter(week), &LoadDay::total);
        indicators.weekLoad = std::accumulate(week.begin(), week.end(), 0);
        if (series.size() >= static_cast<std::size_t>(thresholds.acuteDays)) {
            indicators.monotony = loadMonotony(week);
            if (indicators.monotony) {
                indicators.strain = indicators.weekLoad * *indicators.monotony;
            }
        }
        indicators.breakDays = longestBreak(series, thresholds);
    }
    assessment.verdict = classifyLoad(indicators, thresholds);
    return result;
}

} // namespace fdss::core
