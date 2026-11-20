#pragma once

#include "core/domain/Ids.hpp"

#include <optional>
#include <string>

namespace fdss::core {

// A note of Match Day with its minute (`match_note`, packet P13): it never changes the score or the
// minutes. The minute follows the rules of `match_event` (0-based, `addedTime` over the end of a
// part).
struct MatchNoteDraft {
    int minute = 0;
    std::optional<int> addedTime;
    std::optional<PlayerId> playerId;
    std::string text; // 1–500 characters after trimming

    friend bool operator==(const MatchNoteDraft&, const MatchNoteDraft&) = default;
};

inline constexpr int kMaxNoteLength = 500;

struct MatchNote : MatchNoteDraft {
    MatchNoteId id;
    MatchId matchId;
    // The order of entry among the actions of Match Day; empty for notes written otherwise.
    std::optional<int> seq;

    friend bool operator==(const MatchNote&, const MatchNote&) = default;
};

} // namespace fdss::core
