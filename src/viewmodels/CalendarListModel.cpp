#include "viewmodels/CalendarListModel.hpp"

#include "viewmodels/IsoDates.hpp"

#include <QCoreApplication>
#include <QEvent>

#include <algorithm>
#include <tuple>
#include <utility>

namespace fdss::viewmodels {

CalendarListModel::CalendarListModel(QObject* parent) : QAbstractListModel(parent) {
    // Installing a translator sends QEvent::LanguageChange to the application object, which only
    // an application-wide filter sees; eventFilter() ignores the copies Qt sends on to the items.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
}

int CalendarListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

QVariant CalendarListModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const CalendarEntry& entry = m_entries[static_cast<std::size_t>(index.row())];
    const core::Date date = CalendarTexts::date(entry);
    switch (role) {
    case KindRole:
        return CalendarTexts::kind(entry);
    case ItemIdRole:
        return QVariant::fromValue<qint64>(CalendarTexts::id(entry));
    case DateRole:
        return IsoDates::toIso(date);
    case DateTextRole:
        return QStringLiteral("%1, %2").arg(CalendarTexts::dayName(date),
                                            IsoDates::displayText(date));
    case TimeRole:
        return IsoDates::timeToIso(CalendarTexts::time(entry));
    case KindTextRole:
        return CalendarTexts::kindName(entry);
    case TitleRole:
        return CalendarTexts::title(entry);
    case DetailsRole:
        return CalendarTexts::details(entry);
    case StatusKeyRole:
        return CalendarTexts::statusKey(entry);
    case StatusTextRole:
        return CalendarTexts::statusName(entry);
    case SectionRole:
        return date >= m_today ? tr("Upcoming") : tr("Past");
    default:
        return {};
    }
}

QHash<int, QByteArray> CalendarListModel::roleNames() const {
    return {{KindRole, "kind"},           {ItemIdRole, "itemId"},
            {DateRole, "date"},           {DateTextRole, "dateText"},
            {TimeRole, "time"},           {KindTextRole, "kindText"},
            {TitleRole, "title"},         {DetailsRole, "details"},
            {StatusKeyRole, "statusKey"}, {StatusTextRole, "statusText"},
            {SectionRole, "section"}};
}

void CalendarListModel::setEntries(std::vector<CalendarEntry> entries, core::Date today) {
    std::ranges::stable_sort(entries, [today](const CalendarEntry& a, const CalendarEntry& b) {
        const core::Date dateA = CalendarTexts::date(a);
        const core::Date dateB = CalendarTexts::date(b);
        if (dateA == dateB) {
            return earlierOnTheDay(a, b);
        }
        const bool upcomingA = dateA >= today;
        const bool upcomingB = dateB >= today;
        if (upcomingA != upcomingB) {
            return upcomingA;
        }
        return upcomingA ? dateA < dateB : dateB < dateA;
    });
    beginResetModel();
    m_entries = std::move(entries);
    m_today = today;
    endResetModel();
}

bool CalendarListModel::earlierOnTheDay(const CalendarEntry& a, const CalendarEntry& b) {
    const auto key = [](const CalendarEntry& entry) {
        const auto time = CalendarTexts::time(entry);
        return std::tuple(!time.has_value(), time.value_or(core::TimeOfDay{}), entry.index(),
                          CalendarTexts::id(entry));
    };
    return key(a) < key(b);
}

bool CalendarListModel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == QCoreApplication::instance() && event->type() == QEvent::LanguageChange &&
        !m_entries.empty()) {
        emit dataChanged(index(0), index(rowCount() - 1));
    }
    return QAbstractListModel::eventFilter(watched, event);
}

} // namespace fdss::viewmodels
