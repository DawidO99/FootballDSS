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
    // Minutes over the end of the part of the match, for display only: `minute` then holds the end
    // of that part (packet P13). Empty, or at least 1.
    std::optional<int> addedTime;

    friend bool operator==(const MatchEventDraft&, const MatchEventDraft&) = default;
};

struct MatchEvent : MatchEventDraft {
    MatchEventId id;
    MatchId matchId;
    // The order of entry among the actions of Match Day (the rows of one action share it); empty
    // for events written otherwise (imports, P14 corrections).
    std::optional<int> seq;

    friend bool operator==(const MatchEvent&, const MatchEvent&) = default;
};

} // namespace fdss::core
