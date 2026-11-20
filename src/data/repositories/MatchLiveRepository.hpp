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
    bool clockless = false;
    std::vector<ClockStepRow> steps; // in the order they were taken
    // The action that finished the match; empty while it is live.
    std::optional<int> finishedSeq;
    // The final minute of a match ended with fewer than 7 players; empty for a normal end.
    std::optional<int> endedEarlyAt;

    friend bool operator==(const LiveMatchRow&, const LiveMatchRow&) = default;
};

// The clock of a match that Match Day has started (`match_live`, `match_clock_step`; packet P13).
// Writes join the transaction of the caller; the service owns it.
class MatchLiveRepository {
public:
    explicit MatchLiveRepository(DatabaseManager& db) : m_db(db) {}

    // A second row for the match: Conflict. A missing match or a format out of range: InvalidInput.
    [[nodiscard]] core::Status create(core::MatchId match, core::MatchFormat format,
                                      bool clockless = false);
    // The steps go with it. Nothing to remove: NotFound.
    [[nodiscard]] core::Status remove(core::MatchId match);
    [[nodiscard]] core::Result<std::optional<LiveMatchRow>> find(core::MatchId match) const;
    // A step taken twice: Conflict. No live row: InvalidInput.
    [[nodiscard]] core::Status appendStep(core::MatchId match, core::ClockStep step,
                                          core::Timestamp at, int seq);
    // Another time of a step taken ("set clock"); its seq stays. Not taken: NotFound.
    [[nodiscard]] core::Status moveStep(core::MatchId match, core::ClockStep step,
                                        core::Timestamp at);
    // Removes the steps of one action and returns them (none: empty).
    [[nodiscard]] core::Result<std::vector<ClockStepRow>> deleteSteps(core::MatchId match, int seq);
    // seq nullopt: live again (and not ended early).
    [[nodiscard]] core::Status setFinished(core::MatchId match, std::optional<int> seq,
                                           std::optional<int> endedEarlyAt = std::nullopt);
    // The seq of a new action: the counter of the match, which only grows, so a seq is never used
    // again (P13 re-review). No live row: NotFound.
    [[nodiscard]] core::Result<int> takeSeq(core::MatchId match);

private:
    DatabaseManager& m_db;
};

} // namespace fdss::data
