#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

namespace fdss::viewmodels {

// A list for QML whose values a viewmodel computes: one QVariantList per row, in the order of the
// role names. setRows() keeps the rows that stay and emits dataChanged for them, so delegates and
// the focus in a view stay where they are; only the rows beyond the new count go or come.
class RowsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by a viewmodel")

public:
    RowsModel(QList<QByteArray> roleNames, QObject* parent);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    // Every row has one value per role name.
    void setRows(QList<QVariantList> rows);
    // The value under a role name; invalid out of range.
    [[nodiscard]] QVariant value(int row, const QByteArray& roleName) const;
    // The row under the role names; empty out of range. For the views and the tests.
    Q_INVOKABLE QVariantMap get(int row) const;

private:
    QList<QByteArray> m_roleNames;
    QList<QVariantList> m_rows;
};

// [{ value, text }] for a SelectField whose options are data (matches, templates).
class ChoiceListModel : public RowsModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by a viewmodel")

public:
    explicit ChoiceListModel(QObject* parent = nullptr);
    // -1 when no row has the value.
    [[nodiscard]] int indexOf(const QVariant& value) const;
};

} // namespace fdss::viewmodels
