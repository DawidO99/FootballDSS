#pragma once

#include "viewmodels/RowsModel.hpp"

#include <QtQmlIntegration/qqmlintegration.h>

// The lists of the Match Day screen (packet P13). MatchDayViewModel computes their rows; the role
// names below are what the delegates read.

namespace fdss::viewmodels {

// Players as tiles: playerId, shirtNumber (or nothing), name, shortName (surname), positionCode,
// stateKey (onPitch, bench, substitutedOff, sentOff), stateText, yellowCards, goals, minutesText
// ("67'" played so far, empty for the bench), selectable (a choice of the pending action).
class MatchDayPlayersModel : public RowsModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by MatchDayViewModel")

public:
    explicit MatchDayPlayersModel(QObject* parent = nullptr);
};

// The events, the newest first: eventId, minuteText ("45+3'"), typeKey (goal, opponentGoal,
// yellow, red, secondYellow, sub, assist), iconName, typeText, playerText, relatedText ("Assist:
// …", "Off: …" or empty).
class MatchTimelineModel : public RowsModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by MatchDayViewModel")

public:
    explicit MatchTimelineModel(QObject* parent = nullptr);
};

} // namespace fdss::viewmodels
