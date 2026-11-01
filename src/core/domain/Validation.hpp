#pragma once

#include "core/domain/Appearance.hpp"
#include "core/domain/Attendance.hpp"
#include "core/domain/Availability.hpp"
#include "core/domain/Club.hpp"
#include "core/domain/Match.hpp"
#include "core/domain/MatchEvent.hpp"
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

} // namespace fdss::core
