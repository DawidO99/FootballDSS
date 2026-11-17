#include "viewmodels/MatchDayModels.hpp"

namespace fdss::viewmodels {

MatchDayPlayersModel::MatchDayPlayersModel(QObject* parent)
    : RowsModel({"playerId", "shirtNumber", "name", "shortName", "positionCode", "stateKey",
                 "stateText", "yellowCards", "goals", "minutesText", "selectable"},
                parent) {}

MatchTimelineModel::MatchTimelineModel(QObject* parent)
    : RowsModel(
          {"eventId", "minuteText", "typeKey", "iconName", "typeText", "playerText", "relatedText"},
          parent) {}

} // namespace fdss::viewmodels
