#include "viewmodels/MatchReportModels.hpp"

namespace fdss::viewmodels {

MatchRatingsModel::MatchRatingsModel(QObject* parent)
    : RowsModel({"playerId", "shirtNumber", "playerName", "positionCode", "started", "minutes",
                 "minutesText", "goals", "assists", "yellowCards", "redCards", "rating", "rpe",
                 "rowError", "changed"},
                parent) {}

MatchShapesModel::MatchShapesModel(QObject* parent)
    : RowsModel({"formation", "fromText", "toText", "minutes"}, parent) {}

} // namespace fdss::viewmodels
