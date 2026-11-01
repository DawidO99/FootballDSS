#pragma once

#include "core/domain/Enums.hpp"

#include <QCollator>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace fdss::viewmodels {

class SquadListModel;

enum class StatusGroup { All, Available, Unavailable };

// Filters and sorts a SquadListModel for the table (AGENTS section 6: not in QML).
class SquadFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by a viewmodel")

public:
    explicit SquadFilterModel(SquadListModel* source, QObject* parent = nullptr);

    // Words in the name, in any order and without regard to case or Polish letters. Only digits:
    // exactly that shirt number.
    void setSearchText(const QString& text);
    void setStatusGroup(StatusGroup group);
    // nullopt: every position. Looks at the primary position.
    void setPositionGroup(std::optional<core::PositionGroup> group);
    // An archived player passes only with this on, and then only on StatusGroup::All, having no
    // status of today.
    void setShowArchived(bool show);
    // A role of SquadListModel. Rows without a value (no number, no age) stay last in both
    // directions.
    void sortByRole(int role, Qt::SortOrder order);

protected:
    [[nodiscard]] bool filterAcceptsRow(int sourceRow,
                                        const QModelIndex& sourceParent) const override;
    [[nodiscard]] bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
    [[nodiscard]] const SquadListModel& source() const;

    SquadListModel* m_source;
    QCollator m_collator;
    QStringList m_searchWords;
    bool m_searchIsNumber = false;
    StatusGroup m_statusGroup = StatusGroup::All;
    std::optional<core::PositionGroup> m_positionGroup;
    bool m_showArchived = false;
};

} // namespace fdss::viewmodels
