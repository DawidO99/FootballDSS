#pragma once

#include "viewmodels/RowsModel.hpp"

#include <QtQmlIntegration/qqmlintegration.h>

// The lists of the Pre-match screen (packet P12). PreMatchViewModel computes their rows; the role
// names below are what the delegates read. A player id is 0 for nobody; severity is "error",
// "warning" or empty.

namespace fdss::viewmodels {

// The 11 slots of the board: slotIndex, positionKey (database text), positionText (GK…), pitchX,
// pitchY (0–1, own goal at y = 1), playerId, shirtNumber (or nothing), shortName (surname),
// fullName, severity, issueText (every issue of the slot, one per line), selected.
class TacticsBoardModel : public RowsModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by PreMatchViewModel")

public:
    explicit TacticsBoardModel(QObject* parent = nullptr);
};

// The bench in order: benchIndex, playerId, shirtNumber, name, shortName, positionsText, severity,
// issueText.
class BenchListModel : public RowsModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by PreMatchViewModel")

public:
    explicit BenchListModel(QObject* parent = nullptr);
};

// The squad to pick from, by shirt number: playerId, shirtNumber, name, shortName, positionsText,
// statusKey and statusText (availability on the match date), loadStatusKey and loadStatusText (P11,
// empty while not known), placement ("xi", "bench" or empty) and placementText, severity,
// issueText.
class LineupPlayersModel : public RowsModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by PreMatchViewModel")

public:
    explicit LineupPlayersModel(QObject* parent = nullptr);
};

// The issues of the validator, errors first: severity, severityText, ruleKey, text, playerId,
// slotIndex and benchIndex (-1: none).
class LineupIssuesModel : public RowsModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by PreMatchViewModel")

public:
    explicit LineupIssuesModel(QObject* parent = nullptr);
};

} // namespace fdss::viewmodels
