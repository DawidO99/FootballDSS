#pragma once

#include "core/domain/DateTypes.hpp"
#include "core/domain/Enums.hpp"
#include "core/domain/Ids.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

// Custom metrics (packet P17): what a club measures without code changes.

namespace fdss::core {

inline constexpr std::size_t kMaxMetricNameLength = 50;
inline constexpr std::size_t kMaxMetricUnitLength = 20;
// The player card aggregates over today - 27 ... today.
inline constexpr int kMetricWindowDays = 28;

struct MetricDefinitionDraft {
    // At least one of them; an empty one falls back to the other on display.
    std::string namePl;
    std::string nameEn;
    MetricValueType valueType = MetricValueType::Decimal;
    std::optional<std::string> unit;
    std::optional<double> min;
    std::optional<double> max;
    MetricAggregation aggregation = MetricAggregation::Avg;
    MetricScope scope = MetricScope::Training;
    bool higherIsBetter = true;

    friend bool operator==(const MetricDefinitionDraft&, const MetricDefinitionDraft&) = default;
};

struct MetricDefinition : MetricDefinitionDraft {
    MetricDefinitionId id;
    // Generated from the name once, see metricKey(); it never changes afterwards.
    std::string key;

    friend bool operator==(const MetricDefinition&, const MetricDefinition&) = default;
};

// Where the values of one entry belong: a match (scope match), a training session (scope training)
// or a day (scope player).
using MetricTarget = std::variant<MatchId, TrainingSessionId, Date>;

[[nodiscard]] MetricScope scopeOf(const MetricTarget& target) noexcept;

// One cell of the entry grid. No value: the player has none, and a stored one is deleted.
struct MetricValueDraft {
    PlayerId playerId;
    std::optional<double> value;

    friend bool operator==(const MetricValueDraft&, const MetricValueDraft&) = default;
};

struct MetricValue {
    MetricValueId id;
    MetricDefinitionId metricId;
    PlayerId playerId;
    std::optional<MatchId> matchId;
    std::optional<TrainingSessionId> sessionId;
    Date date{};
    double value = 0.0;

    friend bool operator==(const MetricValue&, const MetricValue&) = default;
};

// ASCII slug of a name: Latin letters folded to lowercase without diacritics, digits kept, every
// run of other characters one '_', none at the ends. "Skok w dał (cm)" is "skok_w_dal_cm". Empty
// when the name has no Latin letter or digit.
[[nodiscard]] std::string metricKey(std::string_view nameUtf8);
// The key of the English name, or of the Polish one when the English name is empty.
[[nodiscard]] std::string metricKey(const MetricDefinitionDraft& draft);

// What makes two metric names "the same" (P17 manual test): Latin case and diacritics folded, every
// space and punctuation mark left out, other letters kept. "Sprint 30m", "sprint 30 m" and
// "Sprint-30M" give "sprint30m". Empty for a name without letters or digits.
[[nodiscard]] std::string metricNameKey(std::string_view nameUtf8);

// What the user typed into a cell. Decimal comma and point both work ("4,35", "4.35", "-1", ",5");
// no thousands separators, no exponent, at most 15 digits. An integer metric needs a whole value
// ("12" or "12,0"); a boolean one "0" or "1". Leading and trailing spaces are ignored. nullopt:
// not a value of that type. The range is checked by validateValue().
[[nodiscard]] std::optional<double> parseMetricValue(std::string_view text, MetricValueType type);

// Boolean metrics count "yes" (sum), give the share of "yes" (avg) or the last answer.
[[nodiscard]] bool isAllowed(MetricValueType type, MetricAggregation aggregation) noexcept;

// `chronological` is oldest first; Last is its last element. nullopt for no values.
[[nodiscard]] std::optional<double> aggregate(std::span<const double> chronological,
                                              MetricAggregation aggregation);

enum class MetricTrend { None, Up, Down, Same };

// None without a previous value.
[[nodiscard]] MetricTrend trendOf(double latest, std::optional<double> previous) noexcept;
// Up when higher is better, Down when lower is better. False for None and Same.
[[nodiscard]] bool isImprovement(MetricTrend trend, bool higherIsBetter) noexcept;

} // namespace fdss::core
