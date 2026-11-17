#pragma once

#include "core/algorithms/MatchMinutes.hpp"
#include "core/domain/DateTypes.hpp"
#include "core/domain/Enums.hpp"
#include "core/domain/Ids.hpp"
#include "core/util/Error.hpp"

#include <optional>
#include <vector>

namespace fdss::data {

class DatabaseManager;

struct ClockStepRow {
    core::ClockStep step;
    core::Timestamp at{};
    int seq = 0;

    friend bool operator==(const ClockStepRow&, const ClockStepRow&) = default;
};

struct LiveMatchRow {
    core::MatchFormat format;
    std::vector<ClockStepRow> steps; // in the order they were taken
    // The action that finished the match; empty while it is live.
    std::optional<int> finishedSeq;
    bool endedEarly = false;

    friend bool operator==(const LiveMatchRow&, const LiveMatchRow&) = default;
};

// The clock of a match that Match Day has started (`match_live`, `match_clock_step`; packet P13).
// Writes join the transaction of the caller; the service owns it.
class MatchLiveRepository {
public:
    explicit MatchLiveRepository(DatabaseManager& db) : m_db(db) {}

    // A second row for the match: Conflict. A missing match or a format out of range: InvalidInput.
    [[nodiscard]] core::Status create(core::MatchId match, core::MatchFormat format);
    // The steps go with it. Nothing to remove: NotFound.
    [[nodiscard]] core::Status remove(core::MatchId match);
    [[nodiscard]] core::Result<std::optional<LiveMatchRow>> find(core::MatchId match) const;
    // A step taken twice: Conflict. No live row: InvalidInput.
    [[nodiscard]] core::Status appendStep(core::MatchId match, core::ClockStep step,
                                          core::Timestamp at, int seq);
    // Removes the steps of one action and returns them (none: empty).
    [[nodiscard]] core::Result<std::vector<ClockStepRow>> deleteSteps(core::MatchId match, int seq);
    // nullopt: live again (and not ended early).
    [[nodiscard]] core::Status setFinished(core::MatchId match, std::optional<int> seq,
                                           bool endedEarly = false);
    // The highest seq of the match over events, notes, clock steps and the finish; 0 for none.
    [[nodiscard]] core::Result<int> lastSeq(core::MatchId match) const;

private:
    DatabaseManager& m_db;
};

} // namespace fdss::data
