#include "viewmodels/StaffListModel.hpp"

#include "core/domain/Enums.hpp"
#include "viewmodels/AppState.hpp"

#include <QCoreApplication>
#include <QEvent>

#include <algorithm>
#include <utility>

namespace fdss::viewmodels {

namespace {

// A letter outside the Basic Multilingual Plane takes two QChars.
QString firstLetter(const QString& word) {
    return word.left(word.front().isHighSurrogate() ? 2 : 1).toUpper();
}

} // namespace

StaffListModel::StaffListModel(QObject* parent) : QAbstractListModel(parent) {
    // Installing a translator sends QEvent::LanguageChange to the application object only.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
}

int StaffListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_staff.size());
}

QVariant StaffListModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const core::StaffUser& user = m_staff[static_cast<std::size_t>(index.row())];
    switch (role) {
    case StaffIdRole:
        return QVariant::fromValue<qint64>(user.id.value);
    case NameRole:
        return QString::fromStdString(user.name);
    case RoleKeyRole:
        return QString::fromUtf8(core::toDbText(user.role));
    case RoleNameRole:
        return AppState::roleDisplayName(user.role);
    case InitialsRole:
        return initials(QString::fromStdString(user.name));
    case ActiveRole:
        return user.active;
    default:
        return {};
    }
}

QHash<int, QByteArray> StaffListModel::roleNames() const {
    return {{StaffIdRole, "staffId"},   {NameRole, "name"},         {RoleKeyRole, "roleKey"},
            {RoleNameRole, "roleName"}, {InitialsRole, "initials"}, {ActiveRole, "active"}};
}

void StaffListModel::setStaff(std::vector<core::StaffUser> staff) {
    beginResetModel();
    m_staff = std::move(staff);
    endResetModel();
}

const std::vector<core::StaffUser>& StaffListModel::staff() const { return m_staff; }

const core::StaffUser* StaffListModel::find(qint64 id) const {
    const auto it =
        std::ranges::find(m_staff, id, [](const core::StaffUser& user) { return user.id.value; });
    return it == m_staff.end() ? nullptr : &*it;
}

QVariantMap StaffListModel::rowAt(int row) const {
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

int StaffListModel::rowOf(qint64 id) const {
    const core::StaffUser* user = find(id);
    return user != nullptr ? static_cast<int>(user - m_staff.data()) : -1;
}

QString StaffListModel::initials(const QString& name) {
    const QStringList words = name.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.isEmpty()) {
        return {};
    }
    return words.size() == 1 ? firstLetter(words.front())
                             : firstLetter(words.front()) + firstLetter(words.back());
}

bool StaffListModel::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::LanguageChange && !m_staff.empty()) {
        emit dataChanged(index(0), index(rowCount() - 1), {RoleNameRole});
    }
    return QAbstractListModel::eventFilter(watched, event);
}

} // namespace fdss::viewmodels
