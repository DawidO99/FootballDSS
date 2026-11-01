#pragma once

#include "core/domain/Appearance.hpp"
#include "core/domain/DateTypes.hpp"
#include "core/domain/Match.hpp"
#include "core/domain/MatchEvent.hpp"
#include "core/util/Error.hpp"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fdss::data {

class DatabaseManager;

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
    // Distinct, the most recently played first.
    [[nodiscard]] core::Result<std::vector<std::string>> listCompetitions(core::TeamId team) const;

    // Replaces all appearances of the match in one transaction: on any error the old rows stay.
    [[nodiscard]] core::Status
    replaceAppearances(core::MatchId match, std::span<const core::AppearanceDraft> appearances);
    [[nodiscard]] core::Result<std::vector<core::Appearance>>
    listAppearances(core::MatchId match) const;

    [[nodiscard]] core::Result<core::MatchEventId> appendEvent(core::MatchId match,
                                                               const core::MatchEventDraft& draft);
    // By minute, then in the order they were added.
    [[nodiscard]] core::Result<std::vector<core::MatchEvent>> listEvents(core::MatchId match) const;
    // Removes the event added last, whatever its minute, and returns it. No events: NotFound.
    [[nodiscard]] core::Result<core::MatchEvent> deleteLastEvent(core::MatchId match);

private:
    DatabaseManager& m_db;
};

} // namespace fdss::data
