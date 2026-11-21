#include "services/MatchRecompute.hpp"

#include "data/repositories/MatchRepository.hpp"
#include "data/repositories/MatchTacticRepository.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <utility>

namespace fdss::services {

std::string_view eventProblemField(core::MatchEventProblem problem) {
    switch (problem) {
    case core::MatchEventProblem::UnknownPlayer:
        return "player_unknown";
    case core::MatchEventProblem::MissingPlayer:
        return "player_missing";
    case core::MatchEventProblem::SamePlayer:
        return "same_player";
    case core::MatchEventProblem::NotOnPitch:
        return "player_not_on_pitch";
    case core::MatchEventProblem::NotOnBench:
        return "player_not_on_bench";
    case core::MatchEventProblem::AlreadySubstituted:
        return "player_substituted";
    case core::MatchEventProblem::SentOff:
        return "player_sent_off";
    case core::MatchEventProblem::MinuteOutOfRange:
        return "minute_out_of_range";
    case core::MatchEventProblem::AfterFinalMinute:
        return "after_final_minute";
    case core::MatchEventProblem::SecondYellowWithoutRed:
        return "second_yellow_without_red";
    }
    return "player_unknown";
}

core::Error eventProblemError(core::MatchEventProblem problem, std::string_view context) {
    const std::string_view field = eventProblemField(problem);
    return core::Error{core::ErrorCode::InvalidInput, std::format("{}: {}", context, field),
                       std::string(field)};
}

// Stored events that do not fit the lineup: the history of the match is inconsistent (written
// outside Match Day, or damaged). Never shown as a lineup with 0:0 (P13 review).
core::Error matchHistoryError(const core::MatchEventError& error, std::string_view context) {
    return core::Error{core::ErrorCode::Database,
                       std::format("{}: stored event {} does not fit the match ({})", context,
                                   error.index, eventProblemField(error.problem)),
                       "match_history"};
}

core::Result<std::vector<core::MatchEventDraft>>
eventActionRows(const core::MatchSheet& sheet, std::span<const core::MatchEvent> stored,
                const core::MatchEventDraft& event, std::optional<int> finalMinute,
                std::optional<EventConflict>& conflictFound) {
    std::vector<core::MatchEventDraft> all(stored.begin(), stored.end());
    const std::size_t storedCount = all.size();
    std::vector<core::MatchEventDraft> rows{event};
    // A second yellow at its time (counted in the order of the match) brings its red row.
    if (event.type == core::MatchEventType::Yellow && event.playerId) {
        all.push_back(event);
        int earlier = 0;
        for (const std::size_t index : core::replayOrder(all)) {
            if (index == storedCount) {
                break;
            }
            if (all[index].type == core::MatchEventType::Yellow &&
                all[index].playerId == event.playerId) {
                ++earlier;
            }
        }
        all.pop_back();
        if (earlier == 1) {
            core::MatchEventDraft red = event;
            red.type = core::MatchEventType::Red;
            red.relatedPlayerId.reset();
            rows.push_back(red);
        }
    }
    all.insert(all.end(), rows.begin(), rows.end());
    const auto replayed = core::replayMatch(sheet, all, finalMinute);
    if (!replayed) {
        const core::MatchEventError& error = replayed.error();
        if (error.index >= storedCount) {
            conflictFound = EventConflict{error.problem, std::nullopt};
            return std::unexpected(eventProblemError(error.problem, "record match event"));
        }
        conflictFound = EventConflict{error.problem, stored[error.index]};
        return std::unexpected(
            core::Error{core::ErrorCode::Conflict,
                        std::format("match event: a later event {} no longer fits ({})",
                                    stored[error.index].id.value, eventProblemField(error.problem)),
                        "event_conflict"});
    }
    return rows;
}

core::Result<core::MatchMinutes> storeMinutes(data::MatchRepository& matches, core::MatchId match,
                                              core::MatchMinutes minutes, bool keepPositions) {
    // Ratings and RPE (packet P14) belong to the player, not to the minutes: they stay.
    const auto existing = matches.listAppearances(match);
    if (!existing) {
        return std::unexpected(existing.error());
    }
    for (core::AppearanceDraft& appearance : minutes.appearances) {
        const auto old =
            std::ranges::find(*existing, appearance.playerId, &core::AppearanceDraft::playerId);
        if (old != existing->end()) {
            appearance.rating = old->rating;
            appearance.rpe = old->rpe;
            if (keepPositions && old->position) {
                appearance.position = old->position;
            }
        }
    }
    if (auto replaced = matches.replaceAppearances(match, minutes.appearances); !replaced) {
        return std::unexpected(replaced.error());
    }
    return minutes;
}

core::Result<core::MatchMinutes> recomputeAppearances(data::MatchRepository& matches,
                                                      data::MatchTacticRepository& tactics,
                                                      core::MatchId match,
                                                      const core::Lineup& lineup,
                                                      std::optional<int> finalMinute) {
    const auto events = matches.listEvents(match);
    if (!events) {
        return std::unexpected(events.error());
    }
    const auto changes = tactics.list(match);
    if (!changes) {
        return std::unexpected(changes.error());
    }
    const std::vector<core::MatchEventDraft> drafts(events->begin(), events->end());
    const std::vector<core::MatchTacticChangeDraft> changeDrafts(changes->begin(), changes->end());
    auto minutes = core::computeMatchMinutes(lineup, drafts, changeDrafts, finalMinute);
    if (!minutes) {
        return std::unexpected(matchHistoryError(minutes.error(), "recompute minutes"));
    }
    return storeMinutes(matches, match, std::move(*minutes), false);
}

} // namespace fdss::services
