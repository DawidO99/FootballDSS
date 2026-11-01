#include "viewmodels/NavigationModel.hpp"

#include <QCoreApplication>
#include <QEvent>

#include <algorithm>

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
    // Installing a translator sends QEvent::LanguageChange to the application object only.
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
    m_currentIndex = index;
    m_currentArgs.clear();
    emit currentIndexChanged();
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
    m_currentIndex = index;
    m_currentArgs = args;
    emit currentIndexChanged();
}

bool NavigationModel::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::LanguageChange && !m_items.empty()) {
        emit dataChanged(index(0), index(rowCount() - 1), {TitleRole});
    }
    return QAbstractListModel::eventFilter(watched, event);
}

} // namespace fdss::viewmodels
