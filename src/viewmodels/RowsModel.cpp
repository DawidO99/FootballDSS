#include "viewmodels/RowsModel.hpp"

#include <algorithm>
#include <utility>

namespace fdss::viewmodels {

RowsModel::RowsModel(QList<QByteArray> roleNames, QObject* parent)
    : QAbstractListModel(parent), m_roleNames(std::move(roleNames)) {}

int RowsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant RowsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_rows.size()) {
        return {};
    }
    const qsizetype column = role - (Qt::UserRole + 1);
    const QVariantList& row = m_rows.at(index.row());
    return column >= 0 && column < row.size() ? row.at(column) : QVariant{};
}

QHash<int, QByteArray> RowsModel::roleNames() const {
    QHash<int, QByteArray> names;
    for (qsizetype i = 0; i < m_roleNames.size(); ++i) {
        names.insert(Qt::UserRole + 1 + static_cast<int>(i), m_roleNames.at(i));
    }
    return names;
}

void RowsModel::setRows(QList<QVariantList> rows) {
    const qsizetype kept = std::min(rows.size(), m_rows.size());
    if (rows.size() < m_rows.size()) {
        beginRemoveRows({}, static_cast<int>(rows.size()), static_cast<int>(m_rows.size() - 1));
        m_rows.resize(rows.size());
        endRemoveRows();
    }
    QList<qsizetype> changed;
    for (qsizetype i = 0; i < kept; ++i) {
        if (m_rows.at(i) != rows.at(i)) {
            m_rows[i] = rows.at(i);
            changed.append(i);
        }
    }
    for (const qsizetype row : changed) {
        emit dataChanged(index(static_cast<int>(row)), index(static_cast<int>(row)));
    }
    if (rows.size() > m_rows.size()) {
        beginInsertRows({}, static_cast<int>(m_rows.size()), static_cast<int>(rows.size() - 1));
        for (qsizetype i = m_rows.size(); i < rows.size(); ++i) {
            m_rows.append(rows.at(i));
        }
        endInsertRows();
    }
}

QVariant RowsModel::value(int row, const QByteArray& roleName) const {
    const qsizetype column = m_roleNames.indexOf(roleName);
    if (row < 0 || row >= m_rows.size() || column < 0 || column >= m_rows.at(row).size()) {
        return {};
    }
    return m_rows.at(row).at(column);
}

QVariantMap RowsModel::get(int row) const {
    QVariantMap values;
    if (row < 0 || row >= m_rows.size()) {
        return values;
    }
    for (qsizetype i = 0; i < m_roleNames.size() && i < m_rows.at(row).size(); ++i) {
        values.insert(QString::fromLatin1(m_roleNames.at(i)), m_rows.at(row).at(i));
    }
    return values;
}

ChoiceListModel::ChoiceListModel(QObject* parent)
    : RowsModel({QByteArrayLiteral("value"), QByteArrayLiteral("text")}, parent) {}

int ChoiceListModel::indexOf(const QVariant& value) const {
    for (int row = 0; row < rowCount(); ++row) {
        if (RowsModel::value(row, QByteArrayLiteral("value")) == value) {
            return row;
        }
    }
    return -1;
}

} // namespace fdss::viewmodels
