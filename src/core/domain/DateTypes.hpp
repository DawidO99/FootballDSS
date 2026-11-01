#pragma once

#include <chrono>
#include <optional>

// Core uses only construction, comparison and sys_days arithmetic of these types (no current_zone,
// zoned_time or std::format of chrono types: Apple Clang). Core never asks for "today"; callers
// pass it in. ISO text lives in fdss_data.

namespace fdss::core {

using Date = std::chrono::year_month_day;
using TimeOfDay = std::chrono::minutes; // since midnight
using Timestamp = std::chrono::sys_seconds;

// The dates the app accepts. year_month_day::ok() alone is not enough: it holds for dates the data
// layer cannot read back, such as year 0, which QDate does not have.
inline constexpr Date kMinSupportedDate{std::chrono::year{1900}, std::chrono::January,
                                        std::chrono::day{1}};
inline constexpr Date kMaxSupportedDate{std::chrono::year{2100}, std::chrono::December,
                                        std::chrono::day{31}};

[[nodiscard]] constexpr bool isSupportedDate(Date date) noexcept {
    return date.ok() && date >= kMinSupportedDate && date <= kMaxSupportedDate;
}

// Both ends included; nullopt means no limit on that side.
struct DateRange {
    std::optional<Date> from;
    std::optional<Date> to;
};

} // namespace fdss::core
