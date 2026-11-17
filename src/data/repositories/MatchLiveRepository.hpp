#pragma once

#include "core/domain/DateTypes.hpp"
#include "core/domain/Enums.hpp"
#include "core/domain/Ids.hpp"
#include "core/util/Error.hpp"

#include <optional>
#include <vector>

namespace fdss::data {

class DatabaseManager;

struct ClockStepRow {
    core::ClockStep step = core::ClockStep::FirstHalfStart;
    core::Timestamp at{};
    // The highest event id of the match when the step was taken; empty without events.
    std::optional<core::MatchEventId> afterEvent;

    friend bool operator==(const ClockStepRow&, const ClockStepRow&) = default;
};

struct LiveMatchRow {
    int halfLength = 45;
    std::vector<ClockStepRow> steps; // in the order they were taken

    friend bool operator==(const LiveMatchRow&, const LiveMatchRow&) = default;
};

// The clock of a match that Match Day has started (`match_live`, `match_clock_step`; packet P13).
// Writes join the transaction of the caller; the service owns it.
class MatchLiveRepository {
public:
    explicit MatchLiveRepository(DatabaseManager& db) : m_db(db) {}

    // A second row for the match: Conflict. A missing match or a half length out of 10–60:
    // InvalidInput.
    [[nodiscard]] core::Status create(core::MatchId match, int halfLength);
    // The steps go with it. Nothing to remove: NotFound.
    [[nodiscard]] core::Status remove(core::MatchId match);
    [[nodiscard]] core::Result<std::optional<LiveMatchRow>> find(core::MatchId match) const;
    // A step taken twice: Conflict. No live row: InvalidInput.
    [[nodiscard]] core::Status appendStep(core::MatchId match, core::ClockStep step,
                                          core::Timestamp at,
                                          std::optional<core::MatchEventId> afterEvent);
    // Removes the step taken last and returns it. No steps: NotFound.
    [[nodiscard]] core::Result<ClockStepRow> deleteLastStep(core::MatchId match);

private:
    DatabaseManager& m_db;
};

} // namespace fdss::data
