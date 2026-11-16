#pragma once

#include "core/algorithms/Load.hpp"

#include <QCollator>
#include <QSortFilterProxyModel>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace fdss::viewmodels {

class LoadListModel;

// Filters the team table by status and sorts it (AGENTS section 6: not in QML).
class LoadFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by a viewmodel")

public:
    explicit LoadFilterModel(LoadListModel* source, QObject* parent = nullptr);

    // nullopt: every status.
    void setStatusFilter(std::optional<core::LoadStatus> status);
    // A role of LoadListModel; a *Text role sorts by its number. Rows without a value stay last in
    // both directions. Equal rows keep the order of the source (by shirt number).
    void sortByRole(int role, Qt::SortOrder order);

protected:
    [[nodiscard]] bool filterAcceptsRow(int sourceRow,
                                        const QModelIndex& sourceParent) const override;
    [[nodiscard]] bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
    [[nodiscard]] const LoadListModel& source() const;

    LoadListModel* m_source;
    QCollator m_collator;
    std::optional<core::LoadStatus> m_statusFilter;
};

} // namespace fdss::viewmodels
