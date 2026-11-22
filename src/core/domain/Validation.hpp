#pragma once

#include "core/domain/Appearance.hpp"
#include "core/domain/Attendance.hpp"
#include "core/domain/Availability.hpp"
#include "core/domain/Club.hpp"
#include "core/domain/Match.hpp"
#include "core/domain/MatchEvent.hpp"
#include "core/domain/Metric.hpp"
#include "core/domain/Opponent.hpp"
#include "core/domain/Player.hpp"
#include "core/domain/StaffUser.hpp"
#include "core/domain/Team.hpp"
#include "core/domain/TrainingSession.hpp"
#include "core/util/Error.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// Pure checks of what the user entered, with no database. `field` is the column name, the same one
// the repositories report in Error::field. An empty list means valid.

namespace fdss::core {

inline constexpr int kMinShirtNumber = 1;
inline constexpr int kMaxShirtNumber = 99;
inline constexpr int kMinBirthYear = 1950;
// The upper limit of birth_year is the current year minus this.
inline constexpr int kYoungestPlayerAge = 4;
inline constexpr int kMinSessionDuration = 15;
inline constexpr int kMaxSessionDuration = 240;
inline constexpr int kMaxRpe = 10;
inline constexpr int kMaxAttendanceMinutes = 240;

struct ValidationError {
    std::string field;
    ErrorCode code = ErrorCode::InvalidInput;

    friend bool operator==(const ValidationError&, const ValidationError&) = default;
};

using ValidationErrors = std::vector<ValidationError>;

// Removes ASCII whitespace from both ends. The repositories store names trimmed this way, so the
// stored length is the validated one.
[[nodiscard]] std::string trimmed(std::string_view utf8);

// Unicode code points, the same as SQLite length(). Bytes would count "ą" twice.
[[nodiscard]] std::size_t characterCount(std::string_view utf8) noexcept;

[[nodiscard]] ValidationErrors validate(const ClubDraft& draft);
[[nodiscard]] ValidationErrors validate(const TeamDraft& draft);
[[nodiscard]] ValidationErrors validate(const StaffUserDraft& draft);
// currentYear: core never asks for "today". The upper limit of birth_year is checked only here.
[[nodiscard]] ValidationErrors validate(const PlayerDraft& draft, int currentYear);
[[nodiscard]] ValidationErrors validate(const AvailabilityDraft& draft);
[[nodiscard]] ValidationErrors validate(const OpponentDraft& draft);
[[nodiscard]] ValidationErrors validate(const MatchDraft& draft);
[[nodiscard]] ValidationErrors validate(const AppearanceDraft& draft);
[[nodiscard]] ValidationErrors validate(const MatchEventDraft& draft);
[[nodiscard]] ValidationErrors validate(const TrainingSessionDraft& draft);
[[nodiscard]] ValidationErrors validate(const AttendanceDraft& draft);
// Fields: "name" (both names empty), "name_pl" / "name_en" (too long), "key" (no Latin letter or
// digit in the name that gives the key), "unit", "min", "max" (not finite, not whole for an integer
// metric, set for a boolean one; "max" also when below min), "aggregation" (see isAllowed()).
[[nodiscard]] ValidationErrors validate(const MetricDefinitionDraft& draft);
// One value of the metric. "value": not finite, not whole for an integer metric, not 0/1 for a
// boolean one. "value_range": below min or above max.
[[nodiscard]] ValidationErrors validateValue(const MetricDefinitionDraft& metric, double value);

} // namespace fdss::core
