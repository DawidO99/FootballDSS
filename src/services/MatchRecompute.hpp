#pragma once

#include "core/algorithms/MatchMinutes.hpp"
#include "core/domain/Appearance.hpp"
#include "core/domain/Ids.hpp"
#include "core/domain/Lineup.hpp"
#include "core/domain/MatchEvent.hpp"
#include "core/util/Error.hpp"

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace fdss::data {
class MatchRepository;
class MatchTacticRepository;
} // namespace fdss::data

// What Match Day (packet P13) and the match report (packet P14) share about the events of a match,
// so there is one implementation of an event action and of the minutes: the rows of one event after
// a full replay, and the appearances recomputed from the events with ratings and RPE kept.

namespace fdss::services {

// Why an event (entered late, at its minute, or added in the report) cannot be recorded.
struct EventConflict {
    core::MatchEventProblem problem = core::MatchEventProblem::UnknownPlayer;
    // A stored event that would no longer fit (e.g. a goal by a player now sent off before it);
    // empty when the new event itself does not fit.
    std::optional<core::MatchEvent> laterEvent;
};

// The error field of a problem: player_unknown, player_not_on_pitch, after_final_minute…
[[nodiscard]] std::string_view eventProblemField(core::MatchEventProblem problem);
// InvalidInput with the field of the problem.
[[nodiscard]] core::Error eventProblemError(core::MatchEventProblem problem,
                                            std::string_view context);
// Stored events that do not fit the match: Database [match_history].
[[nodiscard]] core::Error matchHistoryError(const core::MatchEventError& error,
                                            std::string_view context);

// The rows of one event action: the event, and for a second yellow (counted at its time in the
// order of the match) its red row. They must replay with the stored events; `finalMinute` (a
// finished match) rejects events after it. A failure sets `conflict`: the new event itself does not
// fit (InvalidInput with the field of the problem), or a later stored event no longer does
// (Conflict [event_conflict], `laterEvent` set).
[[nodiscard]] core::Result<std::vector<core::MatchEventDraft>>
eventActionRows(const core::MatchSheet& sheet, std::span<const core::MatchEvent> stored,
                const core::MatchEventDraft& event, std::optional<int> finalMinute,
                std::optional<EventConflict>& conflict);

// Within the transaction of the caller: replaces the appearances of the match with `minutes`,
// keeping the rating and RPE stored for each player and, with `keepPositions`, the stored position
// (an appearance not written by Match Day knows its position better than the sheet rebuilt from
// it).
[[nodiscard]] core::Result<core::MatchMinutes> storeMinutes(data::MatchRepository& matches,
                                                            core::MatchId match,
                                                            core::MatchMinutes minutes,
                                                            bool keepPositions);

// Within the transaction of the caller, from the stored events and tactic changes and the saved
// lineup (core::computeMatchMinutes()), then storeMinutes() without keeping positions.
[[nodiscard]] core::Result<core::MatchMinutes>
recomputeAppearances(data::MatchRepository& matches, data::MatchTacticRepository& tactics,
                     core::MatchId match, const core::Lineup& lineup,
                     std::optional<int> finalMinute);

} // namespace fdss::services
