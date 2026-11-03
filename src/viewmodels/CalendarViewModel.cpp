#include "viewmodels/CalendarViewModel.hpp"

#include "core/domain/Permissions.hpp"
#include "services/Clock.hpp"
#include "services/Session.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/IsoDates.hpp"
#include "viewmodels/ServiceRegistry.hpp"

#include <QCoreApplication>
#include <QEvent>

#include <chrono>
#include <utility>

namespace fdss::viewmodels {

namespace {

constexpr std::chrono::days kWeek{7};

core::Date mondayOf(core::Date date) {
    const std::chrono::sys_days day{date};
    return core::Date{day - (std::chrono::weekday{day} - std::chrono::Monday)};
}

core::Date plusDays(core::Date date, std::chrono::days days) {
    return core::Date{std::chrono::sys_days{date} + days};
}

} // namespace

CalendarViewModel::CalendarViewModel(QObject* parent)
    : QObject(parent), m_calendar(ServiceRegistry::instance().calendar()),
      m_session(ServiceRegistry::instance().session()),
      m_clock(ServiceRegistry::instance().clock()), m_items(new CalendarListModel(this)),
      m_week(new CalendarWeekModel(this)), m_today(m_clock.today()), m_monday(mondayOf(m_today)) {
    // Installing a translator sends QEvent::LanguageChange to the application object only.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
    refreshWeek();
}

qint64 CalendarViewModel::teamId() const { return m_teamId; }

void CalendarViewModel::setTeamId(qint64 teamId) {
    if (teamId == m_teamId) {
        return;
    }
    m_teamId = teamId;
    emit teamIdChanged();
    reload();
}

CalendarListModel* CalendarViewModel::items() const { return m_items; }

CalendarWeekModel* CalendarViewModel::week() const { return m_week; }

QString CalendarViewModel::weekStart() const { return IsoDates::toIso(m_monday); }

QString CalendarViewModel::weekEnd() const {
    return IsoDates::toIso(plusDays(m_monday, kWeek - std::chrono::days{1}));
}

QString CalendarViewModel::weekTitle() const {
    return QStringLiteral("%1 – %2").arg(
        IsoDates::displayText(m_monday),
        IsoDates::displayText(plusDays(m_monday, kWeek - std::chrono::days{1})));
}

bool CalendarViewModel::showsCurrentWeek() const { return m_monday == mondayOf(m_today); }

bool CalendarViewModel::empty() const { return m_entries.empty(); }

QString CalendarViewModel::loadError() const {
    return m_loadFailure ? ErrorPresenter::message(*m_loadFailure) : QString{};
}

void CalendarViewModel::reload() {
    // A view left on the current week stays on it over midnight on Sunday.
    const bool followToday = showsCurrentWeek();
    m_today = m_clock.today();
    if (followToday) {
        m_monday = mondayOf(m_today);
    }
    m_entries.clear();
    m_loadFailure.reset();
    if (m_teamId != 0 && m_session.user()) {
        if (auto items = m_calendar.listItems(core::TeamId{m_teamId})) {
            for (services::CalendarMatch& match : items->matches) {
                m_entries.emplace_back(std::move(match));
            }
            for (core::TrainingSession& session : items->sessions) {
                m_entries.emplace_back(std::move(session));
            }
        } else {
            qCritical("CalendarViewModel: %s", items.error().detail.c_str());
            m_loadFailure = items.error();
        }
    }
    m_items->setEntries(m_entries, m_today);
    refreshWeek();
    emit emptyChanged();
    emit loadErrorChanged();
}

void CalendarViewModel::previousWeek() {
    const core::Date monday = plusDays(m_monday, -kWeek);
    if (core::isSupportedDate(monday)) {
        showWeek(monday);
    }
}

void CalendarViewModel::nextWeek() {
    const core::Date monday = plusDays(m_monday, kWeek);
    if (core::isSupportedDate(monday)) {
        showWeek(monday);
    }
}

void CalendarViewModel::goToToday() {
    m_today = m_clock.today();
    showWeek(mondayOf(m_today));
}

void CalendarViewModel::showWeekOf(const QString& isoDate) {
    const auto date = IsoDates::parse(isoDate);
    if (!date || !core::isSupportedDate(*date)) {
        qWarning("CalendarViewModel: not a date '%s'", qUtf8Printable(isoDate));
        return;
    }
    showWeek(mondayOf(*date));
}

QString CalendarViewModel::defaultNewItemDate() const {
    const core::Date today = m_clock.today();
    return IsoDates::toIso(m_monday == mondayOf(today) ? today : m_monday);
}

QVariantMap CalendarViewModel::rowAt(int row) const {
    if (row < 0 || row >= m_items->rowCount()) {
        return {};
    }
    QVariantMap values;
    const QHash<int, QByteArray> names = m_items->roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it) {
        values.insert(QString::fromLatin1(it.value()), m_items->index(row).data(it.key()));
    }
    return values;
}

int CalendarViewModel::rowOf(const QString& kind, qint64 itemId) const {
    for (int row = 0; row < m_items->rowCount(); ++row) {
        const QModelIndex index = m_items->index(row);
        if (index.data(CalendarListModel::KindRole).toString() == kind &&
            index.data(CalendarListModel::ItemIdRole).toLongLong() == itemId) {
            return row;
        }
    }
    return -1;
}

int CalendarViewModel::attendanceCount(qint64 sessionId) {
    const auto count = m_calendar.attendanceCount(core::TrainingSessionId{sessionId});
    if (!count) {
        report(count.error());
        return -1;
    }
    return *count;
}

QString CalendarViewModel::deleteConfirmationText(int attendanceCount) const {
    return attendanceCount == 1
               ? tr("This session has saved attendance for 1 player. Delete it together with the "
                    "attendance?")
               : tr("This session has saved attendance for %1 players. Delete it together with the "
                    "attendance?")
                     .arg(attendanceCount);
}

bool CalendarViewModel::deleteMatch(qint64 matchId) {
    if (!checkAllowed()) {
        return false;
    }
    auto deleted = m_calendar.deleteMatch(core::MatchId{matchId});
    if (!deleted) {
        report(deleted.error());
        return false;
    }
    m_deleted = std::move(*deleted);
    reload();
    return true;
}

bool CalendarViewModel::deleteSession(qint64 sessionId) {
    if (!checkAllowed()) {
        return false;
    }
    auto deleted = m_calendar.deleteSession(core::TrainingSessionId{sessionId});
    if (!deleted) {
        report(deleted.error());
        return false;
    }
    m_deleted = std::move(*deleted);
    reload();
    return true;
}

bool CalendarViewModel::undoDelete() {
    if (std::holds_alternative<std::monostate>(m_deleted) || !checkAllowed()) {
        return false;
    }
    core::Status status;
    if (const auto* match = std::get_if<services::DeletedMatch>(&m_deleted)) {
        const auto id = m_calendar.restoreMatch(*match);
        status = id ? core::Status{} : core::Status{std::unexpected(id.error())};
    } else {
        const auto id = m_calendar.restoreSession(std::get<services::DeletedSession>(m_deleted));
        status = id ? core::Status{} : core::Status{std::unexpected(id.error())};
    }
    if (!status) {
        report(status.error());
        return false;
    }
    m_deleted = std::monostate{};
    reload();
    return true;
}

bool CalendarViewModel::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        emit weekChanged();
        emit loadErrorChanged();
    }
    return QObject::eventFilter(watched, event);
}

void CalendarViewModel::showWeek(core::Date monday) {
    m_monday = monday;
    refreshWeek();
}

void CalendarViewModel::refreshWeek() {
    m_week->setWeek(m_monday, m_entries, m_today);
    emit weekChanged();
}

bool CalendarViewModel::checkAllowed() {
    if (m_session.isAllowed(core::Permission::EditCalendar)) {
        return true;
    }
    report(core::Error{
        core::ErrorCode::PermissionDenied, "refused by the viewmodel: EditCalendar is needed", {}});
    return false;
}

void CalendarViewModel::report(const core::Error& error) {
    // Data in use is a rule the user ran into; anything else is a real failure.
    if (error.code != core::ErrorCode::Conflict) {
        qCritical("CalendarViewModel: %s", error.detail.c_str());
    }
    emit errorOccurred(ErrorPresenter::message(error));
}

} // namespace fdss::viewmodels
