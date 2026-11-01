#pragma once

#include "core/domain/Enums.hpp"
#include "core/domain/Ids.hpp"

#include <optional>

namespace fdss::core {

struct AppearanceDraft {
    PlayerId playerId;
    bool started = false;
    std::optional<int> minuteOn;
    std::optional<int> minuteOff;
    std::optional<Position> position;
    std::optional<int> rating;
    std::optional<int> rpe;

    friend bool operator==(const AppearanceDraft&, const AppearanceDraft&) = default;
};

struct Appearance : AppearanceDraft {
    AppearanceId id;
    MatchId matchId;

    friend bool operator==(const Appearance&, const Appearance&) = default;
};

} // namespace fdss::core
