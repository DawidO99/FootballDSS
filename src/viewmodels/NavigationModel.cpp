#include "viewmodels/NavigationModel.hpp"

#include <QCoreApplication>
#include <QEvent>

#include <algorithm>
#include <utility>

namespace fdss::viewmodels {

NavigationModel::NavigationModel(QObject* parent)
    : QAbstractListModel(parent),
      m_items{
          {QStringLiteral("dashboard"), QT_TR_NOOP("Dashboard"), QStringLiteral("dashboard")},
          {QStringLiteral("squad"), QT_TR_NOOP("Squad"), QStringLiteral("squad")},
          {QStringLiteral("calendar"), QT_TR_NOOP("Calendar"), QStringLiteral("calendar")},
          {QStringLiteral("training"), QT_TR_NOOP("Training"), QStringLiteral("training")},
          {QStringLiteral("prematch"), QT_TR_NOOP("Pre-match"), QStringLiteral("prematch")},
          {QStringLiteral("matchday"), QT_TR_NOOP("Match Day"), QStringLiteral("matchday")},
          {QStringLiteral("load"), QT_TR_NOOP("Load"), QStringLiteral("load")},
          {QStringLiteral("import"), QT_TR_NOOP("Import"), QStringLiteral("import")},
          {QStringLiteral("settings"), QT_TR_NOOP("Settings"), QStringLiteral("settings")},
      } {
    // Installing a translator sends QEvent::LanguageChange to the application object, which only
    // an application-wide filter sees; eventFilter() ignores the copies Qt sends on to the items.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
}

int NavigationModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_items.size());
}

QVariant NavigationModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const Item& item = m_items[static_cast<std::size_t>(index.row())];
    switch (role) {
    case KeyRole:
        return item.key;
    case TitleRole:
        return tr(item.title);
    case IconNameRole:
        return item.iconName;
    default:
        return {};
    }
}

QHash<int, QByteArray> NavigationModel::roleNames() const {
    return {{KeyRole, "key"}, {TitleRole, "title"}, {IconNameRole, "iconName"}};
}

int NavigationModel::currentIndex() const { return m_currentIndex; }

void NavigationModel::setCurrentIndex(int index) {
    if (index == m_currentIndex) {
        return;
    }
    if (index < 0 || index >= rowCount()) {
        qWarning("NavigationModel: index %d out of range [0, %d)", index, rowCount());
        return;
    }
    moveTo(index, {});
}

QString NavigationModel::currentKey() const {
    return m_items[static_cast<std::size_t>(m_currentIndex)].key;
}

QVariantMap NavigationModel::currentArgs() const { return m_currentArgs; }

void NavigationModel::activate(const QString& key, const QVariantMap& args) {
    const auto it =
        std::ranges::find_if(m_items, [&key](const Item& item) { return item.key == key; });
    if (it == m_items.end()) {
        qWarning("NavigationModel: unknown section '%s'", qUtf8Printable(key));
        return;
    }
    const int index = static_cast<int>(std::distance(m_items.begin(), it));
    if (index == m_currentIndex && args == m_currentArgs) {
        return;
    }
    moveTo(index, args);
}

bool NavigationModel::leaveGuard() const { return m_leaveGuard; }

void NavigationModel::setLeaveGuard(bool guard) {
    if (guard == m_leaveGuard) {
        return;
    }
    m_leaveGuard = guard;
    emit leaveGuardChanged();
}

void NavigationModel::resumeLeave() {
    setLeaveGuard(false);
    if (!m_pendingTarget) {
        return;
    }
    const Target target = std::exchange(m_pendingTarget, std::nullopt).value();
    moveTo(target.index, target.args);
}

void NavigationModel::cancelLeave() {
    m_pendingTarget.reset();
    emit currentIndexChanged();
}

void NavigationModel::moveTo(int index, const QVariantMap& args) {
    if (m_leaveGuard) {
        m_pendingTarget = Target{index, args};
        emit leaveRequested();
        return;
    }
    m_currentIndex = index;
    m_currentArgs = args;
    emit currentIndexChanged();
}

bool NavigationModel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == QCoreApplication::instance() && event->type() == QEvent::LanguageChange &&
        !m_items.empty()) {
        emit dataChanged(index(0), index(rowCount() - 1), {TitleRole});
    }
    return QAbstractListModel::eventFilter(watched, event);
}

} // namespace fdss::viewmodels
