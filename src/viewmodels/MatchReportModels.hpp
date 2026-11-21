#pragma once

#include "viewmodels/RowsModel.hpp"

#include <QtQmlIntegration/qqmlintegration.h>

// The lists of the match report (packet P14). MatchReportViewModel computes their rows; the role
// names below are what the delegates read.

namespace fdss::viewmodels {

// The ratings grid, one row per player who played, in the order of the sheet: playerId,
// shirtNumber (or nothing), playerName, positionCode, started, minutes, minutesText ("90'"),
// goals, assists, yellowCards, redCards, rating and rpe (int or nothing), rowError (translated,
// empty when the row is fine), changed (not saved yet).
class MatchRatingsModel : public RowsModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by MatchReportViewModel")

public:
    enum Column { RatingColumn, RpeColumn };
    Q_ENUM(Column)

    explicit MatchRatingsModel(QObject* parent = nullptr);
};

// The shapes the team played: formation, fromText ("1'"), toText ("63'", or the end), minutes.
class MatchShapesModel : public RowsModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by MatchReportViewModel")

public:
    explicit MatchShapesModel(QObject* parent = nullptr);
};

} // namespace fdss::viewmodels
