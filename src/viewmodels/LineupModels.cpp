#include "viewmodels/LineupModels.hpp"

namespace fdss::viewmodels {

TacticsBoardModel::TacticsBoardModel(QObject* parent)
    : RowsModel({"slotIndex", "positionKey", "positionText", "pitchX", "pitchY", "playerId",
                 "shirtNumber", "shortName", "fullName", "severity", "issueText", "selected"},
                parent) {}

BenchListModel::BenchListModel(QObject* parent)
    : RowsModel({"benchIndex", "playerId", "shirtNumber", "name", "shortName", "positionsText",
                 "severity", "issueText"},
                parent) {}

LineupPlayersModel::LineupPlayersModel(QObject* parent)
    : RowsModel({"playerId", "shirtNumber", "name", "shortName", "positionsText", "statusKey",
                 "statusText", "loadStatusKey", "loadStatusText", "placement", "placementText",
                 "severity", "issueText", "detailsText"},
                parent) {}

LineupIssuesModel::LineupIssuesModel(QObject* parent)
    : RowsModel(
          {"severity", "severityText", "ruleKey", "text", "playerId", "slotIndex", "benchIndex"},
          parent) {}

} // namespace fdss::viewmodels
