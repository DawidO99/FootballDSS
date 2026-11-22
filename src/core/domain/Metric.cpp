#include "core/domain/Metric.hpp"

#include "core/domain/Validation.hpp"
#include "core/parsing/HeaderMatching.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>

namespace fdss::core {

namespace {

// 15 decimal digits always fit a double exactly, so digits / 10^n is the correctly rounded value.
constexpr int kMaxValueDigits = 15;

bool isDigit(char character) noexcept { return character >= '0' && character <= '9'; }

std::optional<double> parseDecimal(std::string_view text) {
    bool negative = false;
    if (!text.empty() && (text.front() == '-' || text.front() == '+')) {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    std::int64_t digits = 0;
    int digitCount = 0;
    int fractionDigits = 0;
    bool separatorSeen = false;
    for (const char character : text) {
        if (character == ',' || character == '.') {
            if (separatorSeen) {
                return std::nullopt;
            }
            separatorSeen = true;
            continue;
        }
        if (!isDigit(character) || ++digitCount > kMaxValueDigits) {
            return std::nullopt;
        }
        digits = digits * 10 + (character - '0');
        if (separatorSeen) {
            ++fractionDigits;
        }
    }
    if (digitCount == 0) {
        return std::nullopt;
    }
    // Powers of ten up to 10^22 are exact doubles; std::pow() does not promise that everywhere.
    double scale = 1.0;
    for (int i = 0; i < fractionDigits; ++i) {
        scale *= 10.0;
    }
    const double value = static_cast<double>(digits) / scale;
    return negative ? -value : value;
}

} // namespace

MetricScope scopeOf(const MetricTarget& target) noexcept {
    switch (target.index()) {
    case 0:
        return MetricScope::Match;
    case 1:
        return MetricScope::Training;
    default:
        return MetricScope::Player;
    }
}

std::string metricKey(std::string_view nameUtf8) {
    std::string key;
    bool pendingSeparator = false;
    for (const char character : foldText(nameUtf8)) {
        const bool keep = (character >= 'a' && character <= 'z') || isDigit(character);
        if (!keep) {
            pendingSeparator = !key.empty();
            continue;
        }
        if (pendingSeparator) {
            key.push_back('_');
            pendingSeparator = false;
        }
        key.push_back(character);
    }
    return key;
}

std::string metricKey(const MetricDefinitionDraft& draft) {
    return metricKey(trimmed(draft.nameEn).empty() ? draft.namePl : draft.nameEn);
}

std::optional<double> parseMetricValue(std::string_view text, MetricValueType type) {
    const std::string value = trimmed(text);
    switch (type) {
    case MetricValueType::Boolean:
        if (value == "0" || value == "1") {
            return value == "1" ? 1.0 : 0.0;
        }
        return std::nullopt;
    case MetricValueType::Integer: {
        const std::optional<double> number = parseDecimal(value);
        if (number && std::trunc(*number) == *number) {
            return number;
        }
        return std::nullopt;
    }
    case MetricValueType::Decimal:
        return parseDecimal(value);
    }
    return std::nullopt;
}

bool isAllowed(MetricValueType type, MetricAggregation aggregation) noexcept {
    if (type != MetricValueType::Boolean) {
        return true;
    }
    return aggregation == MetricAggregation::Sum || aggregation == MetricAggregation::Avg ||
           aggregation == MetricAggregation::Last;
}

std::optional<double> aggregate(std::span<const double> chronological,
                                MetricAggregation aggregation) {
    if (chronological.empty()) {
        return std::nullopt;
    }
    const double sum = std::accumulate(chronological.begin(), chronological.end(), 0.0);
    switch (aggregation) {
    case MetricAggregation::Sum:
        return sum;
    case MetricAggregation::Avg:
        return sum / static_cast<double>(chronological.size());
    case MetricAggregation::Min:
        return std::ranges::min(chronological);
    case MetricAggregation::Max:
        return std::ranges::max(chronological);
    case MetricAggregation::Last:
        return chronological.back();
    }
    return std::nullopt;
}

MetricTrend trendOf(double latest, std::optional<double> previous) noexcept {
    if (!previous) {
        return MetricTrend::None;
    }
    if (latest > *previous) {
        return MetricTrend::Up;
    }
    return latest < *previous ? MetricTrend::Down : MetricTrend::Same;
}

bool isImprovement(MetricTrend trend, bool higherIsBetter) noexcept {
    return (trend == MetricTrend::Up && higherIsBetter) ||
           (trend == MetricTrend::Down && !higherIsBetter);
}

} // namespace fdss::core
