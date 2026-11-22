#pragma once

#include "core/domain/Availability.hpp"

#include <QAbstractListModel>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <vector>

namespace fdss::viewmodels {

// The availability history of a player, newest first.
class AvailabilityListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by a viewmodel")

public:
    // QML uses the role names from roleNames(); the enum is for C++ callers.
    enum DataRole {
        EntryIdRole = Qt::UserRole + 1,
        TypeRole,
        TypeNameRole,
        // ISO strings, empty for no date.
        FromDateRole,
        ExpectedToRole,
        ActualToRole,
        // In the format of the app language.
        FromTextRole,
        ExpectedTextRole,
        ActualTextRole,
        // Empty when there is none, also for a role that may not read it.
        DescriptionRole,
        // The return is not marked yet.
        OpenRole,
        // An open injury or illness whose expected return is before today (P19).
        ReturnOverdueRole
    };

    explicit AvailabilityListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    // `today` decides which entries are past their expected return.
    void setEntries(std::vector<core::Availability> entries, core::Date today);
    // The newest entry past its expected return; nullptr when there is none.
    [[nodiscard]] const core::Availability* firstOverdue() const;
    [[nodiscard]] static bool isReturnOverdue(const core::Availability& entry, core::Date today);
    // nullptr when the id is not in the list.
    [[nodiscard]] const core::Availability* find(qint64 entryId) const;

    // For the selected row of a table: its values under the role names, empty when out of range.
    Q_INVOKABLE QVariantMap rowAt(int row) const;
    // -1 when the id is not in the list.
    Q_INVOKABLE int rowOf(qint64 entryId) const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    std::vector<core::Availability> m_entries;
    core::Date m_today{};
};

} // namespace fdss::viewmodels
