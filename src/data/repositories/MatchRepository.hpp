#pragma once

#include "core/domain/Appearance.hpp"
#include "core/domain/DateTypes.hpp"
#include "core/domain/Match.hpp"
#include "core/domain/MatchEvent.hpp"
#include "core/util/Error.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fdss::data {

class DatabaseManager;

// One appearance of a player with its match (packet P14, the "Matches" tab of the player card).
struct PlayerAppearanceRow {
    core::Appearance appearance;
    core::Match match;
    std::string opponentName;
};

// The rating and RPE of one appearance (packet P14); nullopt clears it.
struct AppearanceRating {
    core::PlayerId player;
    std::optional<int> rating;
    std::optional<int> rpe;
};

class MatchRepository {
public:
    explicit MatchRepository(DatabaseManager& db) : m_db(db) {}

    [[nodiscard]] core::Result<std::vector<core::Match>> list(core::TeamId team,
                                                              core::DateRange range) const;
    [[nodiscard]] core::Result<core::Match> get(core::MatchId id) const;
    [[nodiscard]] core::Result<core::MatchId> create(core::TeamId team,
                                                     const core::MatchDraft& draft);
    // A match that is not planned: Conflict.
    [[nodiscard]] core::Status updatePlanned(core::MatchId id, const core::MatchDraft& draft);
    // A match that is not planned, or that other rows still refer to: Conflict. Appearances and
    // events go with it.
    [[nodiscard]] core::Status removePlanned(core::MatchId id);
    [[nodiscard]] core::Status setResult(core::MatchId id, std::optional<int> goalsFor,
                                         std::optional<int> goalsAgainst, core::MatchStatus status);
    // Only the status; the result stays. A missing match: NotFound.
    [[nodiscard]] core::Status setStatus(core::MatchId id, core::MatchStatus status);
    // The live match of the team (the earliest one, should there be more); nullopt: none.
    [[nodiscard]] core::Result<std::optional<core::MatchId>> findLive(core::TeamId team) const;
    // Distinct, the most recently played first.
    [[nodiscard]] core::Result<std::vector<std::string>> listCompetitions(core::TeamId team) const;

    // Replaces all appearances of the match in one transaction: on any error the old rows stay.
    [[nodiscard]] core::Status
    replaceAppearances(core::MatchId match, std::span<const core::AppearanceDraft> appearances);
    [[nodiscard]] core::Result<std::vector<core::Appearance>>
    listAppearances(core::MatchId match) const;
    // Only the rating and RPE of existing appearances; the minutes stay. A player without an
    // appearance in the match: NotFound. Joins the transaction of the caller.
    [[nodiscard]] core::Status setRatings(core::MatchId match,
                                          std::span<const AppearanceRating> ratings);
    // The appearances of a player in finished matches, the most recent first.
    [[nodiscard]] core::Result<std::vector<PlayerAppearanceRow>>
    listPlayerAppearances(core::PlayerId player) const;

    // The note of the match report (packet P14), plain text; nullopt: none.
    [[nodiscard]] core::Result<std::optional<std::string>> reportNote(core::MatchId match) const;
    [[nodiscard]] core::Status setReportNote(core::MatchId match,
                                             const std::optional<std::string>& note);

    // seq: the order of entry of a Match Day action (P13); empty for other writers.
    [[nodiscard]] core::Result<core::MatchEventId>
    appendEvent(core::MatchId match, const core::MatchEventDraft& draft,
                std::optional<int> seq = std::nullopt);
    // In the order of the match: by minute, at one minute the added time first (by added
    // minutes), then in the order they were added (core::replayOrder()).
    [[nodiscard]] core::Result<std::vector<core::MatchEvent>> listEvents(core::MatchId match) const;
    // Removes the event added last, whatever its minute, and returns it. No events: NotFound.
    [[nodiscard]] core::Result<core::MatchEvent> deleteLastEvent(core::MatchId match);
    // Removes one event and returns it; none in the match: NotFound.
    [[nodiscard]] core::Result<core::MatchEvent> deleteEvent(core::MatchId match,
                                                             core::MatchEventId event);
    // Writes a deleted event back with its id and seq (the undo toast of the report), so it keeps
    // its place in the order of the match. An id in use: Conflict.
    [[nodiscard]] core::Status restoreEvent(const core::MatchEvent& event);
    // Removes the events of one Match Day action (one seq) and returns them; none: empty.
    [[nodiscard]] core::Result<std::vector<core::MatchEvent>>
    deleteEventsWithSeq(core::MatchId match, int seq);

private:
    DatabaseManager& m_db;
};

} // namespace fdss::data
