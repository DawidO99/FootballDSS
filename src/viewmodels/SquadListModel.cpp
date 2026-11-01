#include "viewmodels/SquadListModel.hpp"

#include "viewmodels/SquadTexts.hpp"

#include <QCoreApplication>
#include <QEvent>

#include <algorithm>
#include <utility>

namespace fdss::viewmodels {

SquadListModel::SquadListModel(QObject* parent) : QAbstractListModel(parent) {
    // Installing a translator sends QEvent::LanguageChange to the application object only.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
}

int SquadListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_players.size());
}

QVariant SquadListModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const services::SquadPlayer& entry = playerAt(index.row());
    const core::Player& player = entry.player;
    const bool archived = player.archivedAt.has_value();
    switch (role) {
    case PlayerIdRole:
        return QVariant::fromValue<qint64>(player.id.value);
    case NumberRole:
        return player.shirtNumber ? QVariant(*player.shirtNumber) : QVariant{};
    case NameRole:
        return SquadTexts::displayName(player);
    case PositionRole:
        return SquadTexts::positionCode(player.primaryPosition);
    case SecondaryPositionsRole:
        return SquadTexts::secondaryPositionCodes(player, std::nullopt);
    case PositionTooltipRole:
        return SquadTexts::positionTooltip(player, std::nullopt);
    case AgeRole:
        return SquadTexts::age(player.birthYear, m_currentYear);
    case StatusRole:
        return SquadTexts::statusName(entry.status, archived);
    case StatusKeyRole:
        return SquadTexts::statusKey(entry.status, archived);
    case ArchivedRole:
        return archived;
    default:
        return {};
    }
}

QHash<int, QByteArray> SquadListModel::roleNames() const {
    return {{PlayerIdRole, "playerId"},
            {NumberRole, "number"},
            {NameRole, "name"},
            {PositionRole, "position"},
            {SecondaryPositionsRole, "secondaryPositions"},
            {PositionTooltipRole, "positionTooltip"},
            {AgeRole, "age"},
            {StatusRole, "status"},
            {StatusKeyRole, "statusKey"},
            {ArchivedRole, "archived"}};
}

void SquadListModel::setPlayers(std::vector<services::SquadPlayer> players, int currentYear) {
    beginResetModel();
    m_players = std::move(players);
    m_currentYear = currentYear;
    endResetModel();
}

const services::SquadPlayer& SquadListModel::playerAt(int row) const {
    return m_players[static_cast<std::size_t>(row)];
}

int SquadListModel::rowOf(qint64 playerId) const {
    const auto it = std::ranges::find(m_players, playerId, [](const services::SquadPlayer& entry) {
        return entry.player.id.value;
    });
    return it == m_players.end() ? -1 : static_cast<int>(it - m_players.begin());
}

bool SquadListModel::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::LanguageChange && !m_players.empty()) {
        emit dataChanged(index(0), index(rowCount() - 1), {PositionTooltipRole, StatusRole});
    }
    return QAbstractListModel::eventFilter(watched, event);
}

} // namespace fdss::viewmodels
