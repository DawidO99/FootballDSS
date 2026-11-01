#pragma once

#include "core/domain/DateTypes.hpp"
#include "core/domain/Enums.hpp"
#include "core/domain/Ids.hpp"

#include <optional>

namespace fdss::core {

struct AttendanceDraft {
    PlayerId playerId;
    AttendanceStatus status = AttendanceStatus::Present;
    std::optional<int> rpe;             // empty unless Present
    std::optional<int> minutesOverride; // empty unless Present

    friend bool operator==(const AttendanceDraft&, const AttendanceDraft&) = default;
};

struct Attendance : AttendanceDraft {
    AttendanceId id;
    TrainingSessionId sessionId;

    friend bool operator==(const Attendance&, const Attendance&) = default;
};

// One attendance row with the session data that gives it a meaning over a date range.
struct PlayerAttendance {
    Attendance attendance;
    Date date{};
    int durationMin = 0;

    friend bool operator==(const PlayerAttendance&, const PlayerAttendance&) = default;
};

} // namespace fdss::core
