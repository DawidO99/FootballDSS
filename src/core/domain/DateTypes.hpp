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

// Both ends included; nullopt means no limit on that side.
struct DateRange {
    std::optional<Date> from;
    std::optional<Date> to;
};

} // namespace fdss::core
