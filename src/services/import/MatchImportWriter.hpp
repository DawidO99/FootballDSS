#pragma once

#include "core/domain/Ids.hpp"
#include "core/domain/Opponent.hpp"
#include "core/parsing/MatchImport.hpp"
#include "core/util/Error.hpp"

#include <span>
#include <vector>

namespace fdss::data {
struct ImportReport;
class MatchLiveRepository;
class MatchRepository;
class MetricRepository;
class OpponentRepository;
} // namespace fdss::data

namespace fdss::services {

struct ActivityImportSummary;

// What planMatches() compares with.
struct MatchSnapshot {
    std::vector<core::Opponent> opponents;
    std::vector<core::ExistingMatch> matches;
};

// The database side of the match import (packet P18) for ImportService: what the plan compares
// with, and the writes of a plan inside the transaction of the caller.
class MatchImportWriter {
public:
    MatchImportWriter(data::MatchRepository& matches, data::MatchLiveRepository& matchLive,
                      data::MetricRepository& metrics, data::OpponentRepository& opponents)
        : m_matches(matches), m_matchLive(matchLive), m_metrics(metrics), m_opponents(opponents) {}

    // Every opponent, and the matches of the team on the dates of the import with their
    // appearances and the values of the metrics of the mapping.
    [[nodiscard]] core::Result<MatchSnapshot> snapshot(core::TeamId team,
                                                       const core::MatchImport& import,
                                                       const core::ActivityMapping& mapping) const;

    // `newPlayers`: the ids of plan.newPlayers, in order. New matches are `finished` with the
    // score of the file. Records every row written and the created opponents in `report`.
    [[nodiscard]] core::Status write(core::TeamId team, const core::MatchPlan& plan,
                                     std::span<const core::PlayerId> newPlayers,
                                     core::ImportId importId, data::ImportReport& report,
                                     ActivityImportSummary& summary);

private:
    data::MatchRepository& m_matches;
    data::MatchLiveRepository& m_matchLive;
    data::MetricRepository& m_metrics;
    data::OpponentRepository& m_opponents;
};

} // namespace fdss::services
