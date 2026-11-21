#pragma once

#include "core/domain/Ids.hpp"
#include "core/domain/Lineup.hpp"

#include <optional>

namespace fdss::core {

// A change of the shape during the match (`match_tactic_change`, packet P13b). The 11 slots of the
// saved lineup keep their index for the whole match: `shape` holds their new places (position and
// coordinates) by slot index, without players and without a bench; who stands where comes from the
// lineup and the events (core::shapeAt()). `shape.formation` is the name counted from the occupied
// slots when the change was recorded. The minute follows the rules of `match_event`.
struct MatchTacticChangeDraft {
    int minute = 0;
    std::optional<int> addedTime;
    Lineup shape;

    friend bool operator==(const MatchTacticChangeDraft&, const MatchTacticChangeDraft&) = default;
};

struct MatchTacticChange : MatchTacticChangeDraft {
    MatchTacticChangeId id;
    MatchId matchId;
    // The action of the undo stack.
    int seq = 0;

    friend bool operator==(const MatchTacticChange&, const MatchTacticChange&) = default;
};

} // namespace fdss::core
