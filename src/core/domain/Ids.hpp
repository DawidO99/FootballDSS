#pragma once

#include <compare>
#include <cstdint>

// One type per entity, so a TeamId cannot be passed where a PlayerId is expected.

namespace fdss::core {

struct ClubId {
    std::int64_t value = 0;
    auto operator<=>(const ClubId&) const = default;
};

struct TeamId {
    std::int64_t value = 0;
    auto operator<=>(const TeamId&) const = default;
};

struct StaffUserId {
    std::int64_t value = 0;
    auto operator<=>(const StaffUserId&) const = default;
};

struct PlayerId {
    std::int64_t value = 0;
    auto operator<=>(const PlayerId&) const = default;
};

struct AvailabilityId {
    std::int64_t value = 0;
    auto operator<=>(const AvailabilityId&) const = default;
};

struct OpponentId {
    std::int64_t value = 0;
    auto operator<=>(const OpponentId&) const = default;
};

struct MatchId {
    std::int64_t value = 0;
    auto operator<=>(const MatchId&) const = default;
};

struct AppearanceId {
    std::int64_t value = 0;
    auto operator<=>(const AppearanceId&) const = default;
};

struct MatchEventId {
    std::int64_t value = 0;
    auto operator<=>(const MatchEventId&) const = default;
};

struct TrainingSessionId {
    std::int64_t value = 0;
    auto operator<=>(const TrainingSessionId&) const = default;
};

struct AttendanceId {
    std::int64_t value = 0;
    auto operator<=>(const AttendanceId&) const = default;
};

} // namespace fdss::core
