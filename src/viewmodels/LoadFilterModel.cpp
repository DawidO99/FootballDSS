#include "viewmodels/LoadFilterModel.hpp"

#include "viewmodels/LoadListModel.hpp"

#include <QLocale>

namespace fdss::viewmodels {

namespace {

// The role whose number a *Text role shows.
int numericRole(int role) {
    switch (role) {
    case LoadListModel::AcuteTextRole:
        return LoadListModel::AcuteRole;
    case LoadListModel::ChronicTextRole:
        return LoadListModel::ChronicRole;
    case LoadListModel::RatioTextRole:
        return LoadListModel::RatioRole;
    case LoadListModel::MonotonyTextRole:
        return LoadListModel::MonotonyRole;
    case LoadListModel::WeekLoadTextRole:
        return LoadListModel::WeekLoadRole;
    case LoadListModel::StatusKeyRole:
    case LoadListModel::StatusTextRole:
        return LoadListModel::StatusRankRole;
    default:
        return role;
    }
}

} // namespace

LoadFilterModel::LoadFilterModel(LoadListModel* source, QObject* parent)
    : QSortFilterProxyModel(parent), m_source(source),
      // The names are Polish whatever the language of the app.
      m_collator(QLocale(QLocale::Polish, QLocale::Poland)) {
    m_collator.setCaseSensitivity(Qt::CaseInsensitive);
    setSourceModel(source);
    setDynamicSortFilter(true);
    sortByRole(LoadListModel::StatusRankRole, Qt::AscendingOrder);
}

void LoadFilterModel::setStatusFilter(std::optional<core::LoadStatus> status) {
    beginFilterChange();
    m_statusFilter = status;
    endFilterChange(Direction::Rows);
}

void LoadFilterModel::sortByRole(int role, Qt::SortOrder order) {
    setSortRole(numericRole(role));
    sort(0, order);
}

bool LoadFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex&) const {
    return !m_statusFilter ||
           source().rowAt(sourceRow).assessment.verdict.status == *m_statusFilter;
}

bool LoadFilterModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    if (sortRole() == LoadListModel::NameRole) {
        const core::Player& a = source().rowAt(left.row()).player;
        const core::Player& b = source().rowAt(right.row()).player;
        const int byLastName = m_collator.compare(QString::fromStdString(a.lastName),
                                                  QString::fromStdString(b.lastName));
        return byLastName != 0 ? byLastName < 0
                               : m_collator.compare(QString::fromStdString(a.firstName),
                                                    QString::fromStdString(b.firstName)) < 0;
    }
    const QVariant x = left.data(sortRole());
    const QVariant y = right.data(sortRole());
    // The descending order asks lessThan(right, left), so rows without a value answer by direction.
    if (!x.isValid() || !y.isValid()) {
        return x.isValid() != y.isValid() && (x.isValid() == (sortOrder() == Qt::AscendingOrder));
    }
    return x.toDouble() < y.toDouble();
}

const LoadListModel& LoadFilterModel::source() const { return *m_source; }

} // namespace fdss::viewmodels
