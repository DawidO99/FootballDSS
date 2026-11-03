#include "viewmodels/CalendarWeekModel.hpp"

#include "viewmodels/CalendarListModel.hpp"
#include "viewmodels/IsoDates.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QVariantMap>

#include <algorithm>
#include <chrono>
#include <iterator>

namespace fdss::viewmodels {

namespace {

QVariantMap itemValues(const CalendarEntry& entry) {
    return {{QStringLiteral("kind"), CalendarTexts::kind(entry)},
            {QStringLiteral("itemId"), QVariant::fromValue<qint64>(CalendarTexts::id(entry))},
            {QStringLiteral("time"), IsoDates::timeToIso(CalendarTexts::time(entry))},
            {QStringLiteral("kindText"), CalendarTexts::kindName(entry)},
            {QStringLiteral("title"), CalendarTexts::title(entry)},
            {QStringLiteral("details"), CalendarTexts::details(entry)},
            {QStringLiteral("statusKey"), CalendarTexts::statusKey(entry)},
            {QStringLiteral("statusText"), CalendarTexts::statusName(entry)}};
}

} // namespace

CalendarWeekModel::CalendarWeekModel(QObject* parent) : QAbstractListModel(parent) {
    // Installing a translator sends QEvent::LanguageChange to the application object only.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
}

int CalendarWeekModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : kDaysInWeek;
}

QVariant CalendarWeekModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const Day& day = m_days[static_cast<std::size_t>(index.row())];
    switch (role) {
    case DateRole:
        return IsoDates::toIso(day.date);
    case DayNameRole:
        return CalendarTexts::dayName(day.date);
    case DateTextRole:
        return IsoDates::displayText(day.date);
    case TodayRole:
        return day.date == m_today;
    case ItemsRole: {
        QVariantList items;
        for (const CalendarEntry& entry : day.entries) {
            items.append(itemValues(entry));
        }
        return items;
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> CalendarWeekModel::roleNames() const {
    return {{DateRole, "date"},
            {DayNameRole, "dayName"},
            {DateTextRole, "dateText"},
            {TodayRole, "today"},
            {ItemsRole, "items"}};
}

void CalendarWeekModel::setWeek(core::Date monday, const std::vector<CalendarEntry>& entries,
                                core::Date today) {
    beginResetModel();
    m_today = today;
    std::chrono::sys_days day{monday};
    for (Day& slot : m_days) {
        slot.date = core::Date{day};
        slot.entries.clear();
        std::ranges::copy_if(entries, std::back_inserter(slot.entries),
                             [&slot](const CalendarEntry& entry) {
                                 return CalendarTexts::date(entry) == slot.date;
                             });
        std::ranges::stable_sort(slot.entries, CalendarListModel::earlierOnTheDay);
        day += std::chrono::days{1};
    }
    endResetModel();
}

bool CalendarWeekModel::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        emit dataChanged(index(0), index(rowCount() - 1));
    }
    return QAbstractListModel::eventFilter(watched, event);
}

} // namespace fdss::viewmodels
