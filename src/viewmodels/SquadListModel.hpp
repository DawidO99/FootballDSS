#pragma once

#include "services/SquadService.hpp"

#include <QAbstractListModel>
#include <QtQmlIntegration/qqmlintegration.h>

#include <vector>

namespace fdss::viewmodels {

// The players of a team, archived ones too, in the order of the service. SquadFilterModel sits on
// top of it for the table.
class SquadListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by a viewmodel")

public:
    // QML uses the role names from roleNames(); the enum is for C++ callers.
    enum DataRole {
        PlayerIdRole = Qt::UserRole + 1,
        NumberRole,
        NameRole,
        PositionRole,
        // [{ code, highlighted }]; SquadFilterModel highlights the codes of the active group.
        SecondaryPositionsRole,
        // How many secondary positions the table leaves out ("+N"); 0: none (P19).
        SecondaryMoreRole,
        PositionTooltipRole,
        AgeRole,
        StatusRole,
        StatusKeyRole,
        ArchivedRole
    };

    explicit SquadListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    // currentYear: for the age.
    void setPlayers(std::vector<services::SquadPlayer> players, int currentYear);
    [[nodiscard]] const services::SquadPlayer& playerAt(int row) const;
    // -1 when the id is not in the list.
    [[nodiscard]] int rowOf(qint64 playerId) const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    std::vector<services::SquadPlayer> m_players;
    int m_currentYear = 0;
};

} // namespace fdss::viewmodels
