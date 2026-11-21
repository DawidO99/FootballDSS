#pragma once

#include "viewmodels/RowsModel.hpp"

#include <QtQmlIntegration/qqmlintegration.h>

// The lists of the Match Day screen (packet P13). MatchDayViewModel computes their rows; the role
// names below are what the delegates read.

namespace fdss::viewmodels {

// Players as tiles: playerId, shirtNumber (or nothing), name, shortName (surname), positionCode,
// stateKey (onPitch, bench, substitutedOff, sentOff), stateText, yellowCards, goals, minutesText
// ("67'" played so far, empty for the bench), selectable (a choice of the pending action, by where
// the player was at its minute), historyText ("At 10': on the pitch" when that differs from now).
class MatchDayPlayersModel : public RowsModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by MatchDayViewModel")

public:
    explicit MatchDayPlayersModel(QObject* parent = nullptr);
};

// Events, notes and changes of shape, the latest in the match first: kind (event, note, tactic),
// eventId, noteId, minuteText ("45+3'"), typeKey (goal, opponentGoal, yellow, red, secondYellow,
// sub, assist, note, tactic; for a tactic change playerText is "4-4-2 → 4-2-3-1"),
// iconName, typeText, playerText, relatedText ("Assist: …", "Off: …", the text of a note or empty),
// and for a note its field values: noteMinute ("45+3"), notePlayerId (0: none), noteText; for a
// change of shape its changeId (packet P14; 0 otherwise). MatchTimelineRows builds the rows.
class MatchTimelineModel : public RowsModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by a viewmodel")

public:
    explicit MatchTimelineModel(QObject* parent = nullptr);
};

} // namespace fdss::viewmodels
