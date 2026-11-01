#pragma once

#include "core/domain/Enums.hpp"
#include "core/domain/Ids.hpp"

#include <optional>

namespace fdss::core {

struct MatchEventDraft {
    int minute = 0;
    MatchEventType type = MatchEventType::Goal;
    std::optional<PlayerId> playerId; // empty only for OpponentGoal
    std::optional<PlayerId> relatedPlayerId;

    friend bool operator==(const MatchEventDraft&, const MatchEventDraft&) = default;
};

struct MatchEvent : MatchEventDraft {
    MatchEventId id;
    MatchId matchId;

    friend bool operator==(const MatchEvent&, const MatchEvent&) = default;
};

} // namespace fdss::core
