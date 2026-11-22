#pragma once

#include "core/domain/Ids.hpp"
#include "core/parsing/TrainingImport.hpp"
#include "core/util/Error.hpp"

#include <span>
#include <vector>

namespace fdss::data {
struct ImportReport;
class MetricRepository;
class TrainingRepository;
} // namespace fdss::data

namespace fdss::services {

struct ActivityImportSummary;

// The database side of the training import (packet P18) for ImportService: what the plan compares
// with, and the writes of a plan inside the transaction of the caller.
class TrainingImportWriter {
public:
    TrainingImportWriter(data::TrainingRepository& training, data::MetricRepository& metrics)
        : m_training(training), m_metrics(metrics) {}

    // The sessions of the team on the dates of the import, with their attendance and the values of
    // the metrics of the mapping.
    [[nodiscard]] core::Result<std::vector<core::ExistingSession>>
    snapshot(core::TeamId team, const core::TrainingImport& import,
             const core::ActivityMapping& mapping) const;

    // `newPlayers`: the ids of plan.newPlayers, in order. Records every row written in `report`.
    [[nodiscard]] core::Status write(core::TeamId team, const core::TrainingPlan& plan,
                                     std::span<const core::PlayerId> newPlayers,
                                     core::ImportId importId, data::ImportReport& report,
                                     ActivityImportSummary& summary);

private:
    data::TrainingRepository& m_training;
    data::MetricRepository& m_metrics;
};

} // namespace fdss::services
