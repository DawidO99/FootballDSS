#include "viewmodels/SquadFilterModel.hpp"

#include "viewmodels/SquadListModel.hpp"
#include "viewmodels/SquadTexts.hpp"

#include <QLocale>

#include <algorithm>
#include <limits>
#include <utility>

namespace fdss::viewmodels {

namespace {

// Case and accents gone, so "lukasz" finds "Łukasz". Unicode decomposition does not split "ł".
QString folded(const QString& text) {
    QString result;
    const QString decomposed = text.normalized(QString::NormalizationForm_D);
    result.reserve(decomposed.size());
    for (const QChar character : decomposed) {
        if (character.category() != QChar::Mark_NonSpacing) {
            result.append(character);
        }
    }
    result.replace(QChar(0x0142), u'l');
    result.replace(QChar(0x0141), u'L');
    return result.toCaseFolded();
}

bool isAllDigits(const QString& text) {
    return !text.isEmpty() &&
           std::ranges::all_of(text, [](QChar character) { return character.isDigit(); });
}

// The order of the enumeration, which goes from most to least important; archived rows last.
int statusRank(const services::SquadPlayer& entry) {
    return entry.player.archivedAt ? std::numeric_limits<int>::max()
                                   : static_cast<int>(entry.status);
}

} // namespace

SquadFilterModel::SquadFilterModel(SquadListModel* source, QObject* parent)
    : QSortFilterProxyModel(parent), m_source(source),
      // The names are Polish whatever the language of the app.
      m_collator(QLocale(QLocale::Polish, QLocale::Poland)) {
    m_collator.setCaseSensitivity(Qt::CaseInsensitive);
    setSourceModel(source);
    setDynamicSortFilter(true);
    sortByRole(SquadListModel::NumberRole, Qt::AscendingOrder);
}

void SquadFilterModel::setSearchText(const QString& text) {
    beginFilterChange();
    const QString query = text.trimmed();
    m_searchIsNumber = isAllDigits(query);
    m_searchWords = folded(query).split(u' ', Qt::SkipEmptyParts);
    endFilterChange(Direction::Rows);
}

void SquadFilterModel::setStatusGroup(StatusGroup group) {
    beginFilterChange();
    m_statusGroup = group;
    endFilterChange(Direction::Rows);
}

void SquadFilterModel::setPositionGroup(std::optional<core::PositionGroup> group) {
    beginFilterChange();
    m_positionGroup = group;
    endFilterChange(Direction::Rows);
    // The default order depends on the group, and so does what the remaining rows show.
    invalidate();
    if (rowCount() > 0) {
        emit dataChanged(
            index(0, 0), index(rowCount() - 1, 0),
            {SquadListModel::SecondaryPositionsRole, SquadListModel::PositionTooltipRole});
    }
}

void SquadFilterModel::setShowArchived(bool show) {
    beginFilterChange();
    m_showArchived = show;
    endFilterChange(Direction::Rows);
}

void SquadFilterModel::sortByRole(int role, Qt::SortOrder order) {
    setSortRole(role);
    sort(0, order);
}

QVariant SquadFilterModel::data(const QModelIndex& index, int role) const {
    if (m_positionGroup && index.isValid()) {
        const core::Player& player = source().playerAt(mapToSource(index).row()).player;
        if (role == SquadListModel::SecondaryPositionsRole) {
            return SquadTexts::secondaryPositionCodes(player, m_positionGroup);
        }
        if (role == SquadListModel::PositionTooltipRole) {
            return SquadTexts::positionTooltip(player, m_positionGroup);
        }
    }
    return QSortFilterProxyModel::data(index, role);
}

namespace {

// A player without a primary position has it in no group (P19).
bool primaryIn(const core::Player& player, core::PositionGroup group) {
    return player.primaryPosition && core::positionGroup(*player.primaryPosition) == group;
}

std::optional<int> positionRank(const core::Player& player) {
    return player.primaryPosition ? std::optional(static_cast<int>(*player.primaryPosition))
                                  : std::nullopt;
}

} // namespace

bool SquadFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex&) const {
    const services::SquadPlayer& entry = source().playerAt(sourceRow);
    const core::Player& player = entry.player;
    if (player.archivedAt && (!m_showArchived || m_statusGroup != StatusGroup::All)) {
        return false;
    }
    if (!player.archivedAt && m_statusGroup != StatusGroup::All &&
        (entry.status == core::PlayerStatus::Available) !=
            (m_statusGroup == StatusGroup::Available)) {
        return false;
    }
    if (m_positionGroup && !primaryIn(player, *m_positionGroup) &&
        !std::ranges::any_of(player.secondaryPositions, [this](core::Position position) {
            return core::positionGroup(position) == *m_positionGroup;
        })) {
        return false;
    }
    if (m_searchIsNumber) {
        return player.shirtNumber && *player.shirtNumber == m_searchWords.front().toInt();
    }
    const QString name = folded(SquadTexts::displayName(player));
    return std::ranges::all_of(m_searchWords,
                               [&name](const QString& word) { return name.contains(word); });
}

bool SquadFilterModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    const services::SquadPlayer& a = source().playerAt(left.row());
    const services::SquadPlayer& b = source().playerAt(right.row());
    // The descending order asks lessThan(right, left), so rows without a value answer by direction.
    const auto compareOptional = [this](const std::optional<int>& x, const std::optional<int>& y,
                                        bool inverted) {
        if (!x || !y) {
            return x.has_value() != y.has_value() &&
                   (x.has_value() == (sortOrder() == Qt::AscendingOrder));
        }
        return inverted ? *y < *x : *x < *y;
    };
    switch (sortRole()) {
    case SquadListModel::NumberRole:
        if (m_positionGroup && sortOrder() == Qt::AscendingOrder) {
            const bool aPrimary = primaryIn(a.player, *m_positionGroup);
            const bool bPrimary = primaryIn(b.player, *m_positionGroup);
            if (aPrimary != bPrimary) {
                return aPrimary;
            }
        }
        return compareOptional(a.player.shirtNumber, b.player.shirtNumber, false);
    case SquadListModel::AgeRole:
        // A higher birth year is a lower age.
        return compareOptional(a.player.birthYear, b.player.birthYear, true);
    case SquadListModel::NameRole: {
        const int byLastName = m_collator.compare(QString::fromStdString(a.player.lastName),
                                                  QString::fromStdString(b.player.lastName));
        return byLastName != 0 ? byLastName < 0
                               : m_collator.compare(QString::fromStdString(a.player.firstName),
                                                    QString::fromStdString(b.player.firstName)) < 0;
    }
    case SquadListModel::PositionRole:
        // Players without a position last, in both directions.
        return compareOptional(positionRank(a.player), positionRank(b.player), false);
    case SquadListModel::StatusRole:
        return statusRank(a) < statusRank(b);
    default:
        return false;
    }
}

const SquadListModel& SquadFilterModel::source() const { return *m_source; }

} // namespace fdss::viewmodels
