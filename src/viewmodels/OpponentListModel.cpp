#include "viewmodels/OpponentListModel.hpp"

#include <QLocale>
#include <QString>

#include <algorithm>
#include <utility>

namespace fdss::viewmodels {

OpponentListModel::OpponentListModel(QObject* parent)
    : QAbstractListModel(parent),
      // The names are Polish whatever the language of the app.
      m_collator(QLocale(QLocale::Polish, QLocale::Poland)) {
    m_collator.setCaseSensitivity(Qt::CaseInsensitive);
}

int OpponentListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_opponents.size());
}

QVariant OpponentListModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const core::Opponent& opponent = m_opponents[static_cast<std::size_t>(index.row())];
    switch (role) {
    case ValueRole:
        return QVariant::fromValue<qint64>(opponent.id.value);
    case TextRole:
        return QString::fromStdString(opponent.name);
    default:
        return {};
    }
}

QHash<int, QByteArray> OpponentListModel::roleNames() const {
    return {{ValueRole, "value"}, {TextRole, "text"}};
}

void OpponentListModel::setOpponents(std::vector<core::Opponent> opponents) {
    std::ranges::stable_sort(opponents, [this](const core::Opponent& a, const core::Opponent& b) {
        return m_collator.compare(QString::fromStdString(a.name), QString::fromStdString(b.name)) <
               0;
    });
    beginResetModel();
    m_opponents = std::move(opponents);
    endResetModel();
}

} // namespace fdss::viewmodels
