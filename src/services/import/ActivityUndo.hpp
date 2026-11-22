#pragma once

#include "core/util/Error.hpp"

namespace fdss::data {
struct ImportReport;
class MatchRepository;
class MetricRepository;
class TrainingRepository;
} // namespace fdss::data

namespace fdss::services {

struct ImportUndoPreview;
struct ImportUndoSummary;

// The rows of a training or match import in its report_json (packet P18 decisions 3 and 10).
struct ActivityRowStores {
    data::TrainingRepository& training;
    data::MatchRepository& matches;
    data::MetricRepository& metrics;
};

// Rows the import created that hold other values now (an RPE or a rating entered by hand): added
// to `preview` as entries and values.
[[nodiscard]] core::Status countChangedRows(const data::ImportReport& report,
                                            const ActivityRowStores& stores,
                                            ImportUndoPreview& preview);

// Rows the import overwrote get their values before back while they still hold the values the
// import wrote; otherwise they stay and count as not reverted. Joins the transaction of the caller.
[[nodiscard]] core::Status restoreOverwrittenRows(const data::ImportReport& report,
                                                  const ActivityRowStores& stores,
                                                  ImportUndoSummary& summary);

} // namespace fdss::services
