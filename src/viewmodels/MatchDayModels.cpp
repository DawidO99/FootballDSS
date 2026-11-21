#include "viewmodels/MatchDayModels.hpp"

namespace fdss::viewmodels {

MatchDayPlayersModel::MatchDayPlayersModel(QObject* parent)
    : RowsModel({"playerId", "shirtNumber", "name", "shortName", "positionCode", "stateKey",
                 "stateText", "yellowCards", "goals", "minutesText", "selectable", "historyText"},
                parent) {}

MatchTimelineModel::MatchTimelineModel(QObject* parent)
    : RowsModel({"kind", "eventId", "noteId", "minuteText", "typeKey", "iconName", "typeText",
                 "playerText", "relatedText", "noteMinute", "notePlayerId", "noteText", "changeId"},
                parent) {}

} // namespace fdss::viewmodels
