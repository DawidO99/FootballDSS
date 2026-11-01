#include "viewmodels/AvailabilityListModel.hpp"

#include "viewmodels/IsoDates.hpp"
#include "viewmodels/SquadTexts.hpp"

#include <QCoreApplication>
#include <QEvent>

#include <algorithm>
#include <string>
#include <utility>

namespace fdss::viewmodels {

AvailabilityListModel::AvailabilityListModel(QObject* parent) : QAbstractListModel(parent) {
    // Installing a translator sends QEvent::LanguageChange to the application object only.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
}

int AvailabilityListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

QVariant AvailabilityListModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const core::Availability& entry = m_entries[static_cast<std::size_t>(index.row())];
    switch (role) {
    case EntryIdRole:
        return QVariant::fromValue<qint64>(entry.id.value);
    case TypeRole:
        return QString::fromUtf8(core::toDbText(entry.type));
    case TypeNameRole:
        return SquadTexts::availabilityTypeName(entry.type);
    case FromDateRole:
        return IsoDates::toIso(entry.fromDate);
    case ExpectedToRole:
        return IsoDates::toIso(entry.expectedTo);
    case ActualToRole:
        return IsoDates::toIso(entry.actualTo);
    case FromTextRole:
        return IsoDates::displayText(entry.fromDate);
    case ExpectedTextRole:
        return IsoDates::displayText(entry.expectedTo);
    case ActualTextRole:
        return IsoDates::displayText(entry.actualTo);
    case DescriptionRole:
        return QString::fromStdString(entry.description.value_or(std::string{}));
    case OpenRole:
        return !entry.actualTo.has_value();
    default:
        return {};
    }
}

QHash<int, QByteArray> AvailabilityListModel::roleNames() const {
    return {{EntryIdRole, "entryId"},
            {TypeRole, "type"},
            {TypeNameRole, "typeName"},
            {FromDateRole, "fromDate"},
            {ExpectedToRole, "expectedTo"},
            {ActualToRole, "actualTo"},
            {FromTextRole, "fromText"},
            {ExpectedTextRole, "expectedText"},
            {ActualTextRole, "actualText"},
            {DescriptionRole, "description"},
            {OpenRole, "open"}};
}

void AvailabilityListModel::setEntries(std::vector<core::Availability> entries) {
    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
}

const core::Availability* AvailabilityListModel::find(qint64 entryId) const {
    const auto it = std::ranges::find(
        m_entries, entryId, [](const core::Availability& entry) { return entry.id.value; });
    return it == m_entries.end() ? nullptr : &*it;
}

QVariantMap AvailabilityListModel::rowAt(int row) const {
    if (row < 0 || row >= rowCount()) {
        return {};
    }
    QVariantMap values;
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it) {
        values.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    }
    return values;
}

int AvailabilityListModel::rowOf(qint64 entryId) const {
    const core::Availability* entry = find(entryId);
    return entry != nullptr ? static_cast<int>(entry - m_entries.data()) : -1;
}

bool AvailabilityListModel::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::LanguageChange && !m_entries.empty()) {
        emit dataChanged(index(0), index(rowCount() - 1),
                         {TypeNameRole, FromTextRole, ExpectedTextRole, ActualTextRole});
    }
    return QAbstractListModel::eventFilter(watched, event);
}

} // namespace fdss::viewmodels
