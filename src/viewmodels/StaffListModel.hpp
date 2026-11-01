#pragma once

#include "core/domain/StaffUser.hpp"

#include <QAbstractListModel>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <vector>

namespace fdss::viewmodels {

// Staff users for QML, as the profile picker and the Staff settings show them.
class StaffListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by a viewmodel")

public:
    // QML uses the role names from roleNames(); the enum is for C++ callers.
    enum DataRole {
        StaffIdRole = Qt::UserRole + 1,
        NameRole,
        RoleKeyRole,
        RoleNameRole,
        InitialsRole,
        ActiveRole
    };

    explicit StaffListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void setStaff(std::vector<core::StaffUser> staff);
    [[nodiscard]] const std::vector<core::StaffUser>& staff() const;
    // nullptr when the id is not in the list.
    [[nodiscard]] const core::StaffUser* find(qint64 id) const;

    // For the selected row of a table: its values under the role names, empty when out of range.
    Q_INVOKABLE QVariantMap rowAt(int row) const;
    // -1 when the id is not in the list.
    Q_INVOKABLE int rowOf(qint64 id) const;

    // The first letters of the first and the last word of a name.
    [[nodiscard]] static QString initials(const QString& name);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    std::vector<core::StaffUser> m_staff;
};

} // namespace fdss::viewmodels
