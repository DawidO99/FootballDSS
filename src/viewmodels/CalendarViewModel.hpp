#pragma once

#include "core/domain/DateTypes.hpp"
#include "core/util/Error.hpp"
#include "services/CalendarService.hpp"
#include "viewmodels/CalendarListModel.hpp"
#include "viewmodels/CalendarTexts.hpp"
#include "viewmodels/CalendarWeekModel.hpp"

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>
#include <variant>
#include <vector>

namespace fdss::services {
class Clock;
class Session;
} // namespace fdss::services

namespace fdss::viewmodels {

// The calendar screen: its list and its week, moving between weeks, and deleting with undo.
class CalendarViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // 0: no team, and an empty calendar. The view binds it to AppState.teamId.
    Q_PROPERTY(qint64 teamId READ teamId WRITE setTeamId NOTIFY teamIdChanged FINAL)
    Q_PROPERTY(fdss::viewmodels::CalendarListModel* items READ items CONSTANT FINAL)
    Q_PROPERTY(fdss::viewmodels::CalendarWeekModel* week READ week CONSTANT FINAL)
    // The Monday and the Sunday of the shown week.
    Q_PROPERTY(QString weekStart READ weekStart NOTIFY weekChanged FINAL)
    Q_PROPERTY(QString weekEnd READ weekEnd NOTIFY weekChanged FINAL)
    // In the format of the app language.
    Q_PROPERTY(QString weekTitle READ weekTitle NOTIFY weekChanged FINAL)
    Q_PROPERTY(bool showsCurrentWeek READ showsCurrentWeek NOTIFY weekChanged FINAL)
    // Why the calendar could not be read; the view offers to try again. Empty: it is fine.
    // The team has no matches and no sessions at all, in any week.
    Q_PROPERTY(bool empty READ empty NOTIFY emptyChanged FINAL)
    Q_PROPERTY(QString loadError READ loadError NOTIFY loadErrorChanged FINAL)

public:
    explicit CalendarViewModel(QObject* parent = nullptr);

    [[nodiscard]] qint64 teamId() const;
    void setTeamId(qint64 teamId);
    [[nodiscard]] CalendarListModel* items() const;
    [[nodiscard]] CalendarWeekModel* week() const;
    [[nodiscard]] QString weekStart() const;
    [[nodiscard]] QString weekEnd() const;
    [[nodiscard]] QString weekTitle() const;
    [[nodiscard]] bool showsCurrentWeek() const;
    [[nodiscard]] bool empty() const;
    [[nodiscard]] QString loadError() const;

    Q_INVOKABLE void reload();
    // Past the dates the app supports nothing happens.
    Q_INVOKABLE void previousWeek();
    Q_INVOKABLE void nextWeek();
    Q_INVOKABLE void goToToday();
    // Not a date: nothing happens.
    Q_INVOKABLE void showWeekOf(const QString& isoDate);
    // Today when the shown week holds it, otherwise the Monday of the shown week.
    Q_INVOKABLE QString defaultNewItemDate() const;
    // For a row of the list: its values under the role names, empty when out of range.
    Q_INVOKABLE QVariantMap rowAt(int row) const;
    // The row of an item in the list, by kind (match or session) and id; -1 when it is not there.
    Q_INVOKABLE int rowOf(const QString& kind, qint64 itemId) const;
    // How many players have attendance saved for the session; -1 and errorOccurred when unreadable.
    Q_INVOKABLE int attendanceCount(qint64 sessionId);
    // The question to ask before deleting a session with that many attendance rows.
    Q_INVOKABLE QString deleteConfirmationText(int attendanceCount) const;
    // 1 when a lineup is saved for the match, 0 when not; -1 and errorOccurred when unreadable.
    // A lineup takes a coach time to set up, so a match that has one asks first (P12 review).
    Q_INVOKABLE int matchHasLineup(qint64 matchId);
    // True when deleted; otherwise errorOccurred says why. The item is kept for undoDelete().
    Q_INVOKABLE bool deleteMatch(qint64 matchId);
    Q_INVOKABLE bool deleteSession(qint64 sessionId);
    // Saves the last deleted item again, under a new id. False when there is none or it failed.
    Q_INVOKABLE bool undoDelete();

signals:
    void teamIdChanged();
    void weekChanged();
    void emptyChanged();
    void loadErrorChanged();
    // A translated text for the user; the view shows it in a toast.
    void errorOccurred(const QString& message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    using Deleted = std::variant<std::monostate, services::DeletedMatch, services::DeletedSession>;

    void showWeek(core::Date monday);
    void refreshWeek();
    // Without EditCalendar: errorOccurred, and the service is not called. The service checks the
    // same again (AGENTS section 4).
    [[nodiscard]] bool checkAllowed();
    void report(const core::Error& error);

    services::CalendarService& m_calendar;
    services::Session& m_session;
    const services::Clock& m_clock;
    CalendarListModel* m_items;
    CalendarWeekModel* m_week;

    qint64 m_teamId = 0;
    core::Date m_today{};
    core::Date m_monday{};
    std::vector<CalendarEntry> m_entries;
    std::optional<core::Error> m_loadFailure;
    Deleted m_deleted;
};

} // namespace fdss::viewmodels
