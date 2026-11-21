#pragma once

#include "viewmodels/RowsModel.hpp"

#include <QCollator>
#include <QSortFilterProxyModel>
#include <QString>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

// The lists of the Pre-match screen (packet P12). PreMatchViewModel computes their rows; the role
// names below are what the delegates read. A player id is 0 for nobody; severity is "error",
// "warning" or empty.

namespace fdss::viewmodels {

// The 11 slots of the board: slotIndex, positionKey (database text), positionText (GK…), pitchX,
// pitchY (0–1, own goal at y = 1), playerId, shirtNumber (or nothing), shortName (surname),
// fullName, severity, issueText (every issue of the slot, one per line), selected; for the live
// board of Match Day (P13b) also yellowCards, goals and hidden (an empty slot: a player sent off;
// always false in Pre-match, 0 cards and goals).
class TacticsBoardModel : public RowsModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by PreMatchViewModel or MatchDayViewModel")

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
// positionCode (the primary position), statusKey and statusText (availability on the match date),
// loadStatusKey and loadStatusText (P11, empty while not known), placement ("xi", "bench" or empty)
// and placementText, severity, issueText, detailsText (availability on the match day, load as of
// its day). For the order of LineupPlayersSortModel: section (0 XI, 1 bench, 2 the rest),
// sectionText, sectionOrder (XI: from the goalkeeper up the pitch; bench: its order), lastName,
// firstName, positionRank, statusRank and loadRank (nothing: no value, last).
class LineupPlayersModel : public RowsModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by PreMatchViewModel")

public:
    explicit LineupPlayersModel(QObject* parent = nullptr);
};

// The squad list of the screen (P12 manual test, decision 21): the XI first, from the goalkeeper up
// the pitch, then the bench in its order, then the rest in the order of the chosen column as in
// Squad. Rows without a value for that column stay last in both directions.
class LineupPlayersSortModel : public QSortFilterProxyModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by PreMatchViewModel")
    // number, name, position, status or load.
    Q_PROPERTY(QString sortColumn READ sortColumn NOTIFY sortChanged FINAL)
    Q_PROPERTY(bool ascending READ ascending NOTIFY sortChanged FINAL)

public:
    explicit LineupPlayersSortModel(LineupPlayersModel* source, QObject* parent = nullptr);

    [[nodiscard]] QString sortColumn() const;
    [[nodiscard]] bool ascending() const;
    // The same column again turns the direction; another one starts ascending. Unknown: ignored.
    Q_INVOKABLE void sortBy(const QString& column);
    void setSort(const QString& column, bool ascending);

    // The row under the role names; empty out of range.
    Q_INVOKABLE QVariantMap get(int row) const;
    // -1: not in the list.
    Q_INVOKABLE int rowOf(qint64 playerId) const;
    [[nodiscard]] QVariant value(int row, const QByteArray& roleName) const;

signals:
    void sortChanged();

protected:
    [[nodiscard]] bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
    [[nodiscard]] QVariant sourceValue(const QModelIndex& index, const QByteArray& roleName) const;

    LineupPlayersModel* m_source;
    QCollator m_collator;
    QString m_column = QStringLiteral("number");
    bool m_ascending = true;
};

// The players offered for one slot (P12 manual test, decision 20): group "primary" (the position
// of the slot is their primary one), "secondary", then "other"; in each the available ones by
// shirt number, the unavailable ones last. playerId, shirtNumber, name, positionsText, group,
// groupText, statusKey, statusText, available, loadStatusKey, loadStatusText, placement,
// placementText, current (already in this slot).
class SlotPickerModel : public RowsModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by PreMatchViewModel")

public:
    explicit SlotPickerModel(QObject* parent = nullptr);
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
