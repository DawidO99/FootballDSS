#pragma once

#include "services/DashboardService.hpp"
#include "services/LoadService.hpp"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace fdss::services {
class Session;
} // namespace fdss::services

namespace fdss::viewmodels {

// The dashboard (SPEC section 4, packet P15) and the "days to next match" chip of the top bar.
// Main.qml holds one per session and reloads it with the capabilities. Read-only for every role.
// The card lists are small read-only QVariantLists rebuilt on every read (P15 decision 8).
//
// An empty state is a map {text, actionText, actionSection}: actionText is empty for a role that
// cannot act, and the text then says who can (AGENTS section 6, P15 decision 7).
class DashboardViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(qint64 teamId READ teamId WRITE setTeamId NOTIFY teamIdChanged FINAL)
    // AppSettings.yellowCardThreshold, as in Pre-match (P12 decision 3).
    Q_PROPERTY(int yellowCardThreshold READ yellowCardThreshold WRITE setYellowCardThreshold NOTIFY
                   yellowCardThresholdChanged FINAL)
    // The load signals are read on the thread pool.
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    // False until the overview read succeeded, and after a failed overview read.
    Q_PROPERTY(bool ready READ ready NOTIFY contentChanged FINAL)
    Q_PROPERTY(QString readError READ readError NOTIFY contentChanged FINAL)
    Q_PROPERTY(QString loadError READ loadError NOTIFY contentChanged FINAL)

    Q_PROPERTY(bool hasNextMatch READ hasNextMatch NOTIFY contentChanged FINAL)
    Q_PROPERTY(qint64 nextMatchId READ nextMatchId NOTIFY contentChanged FINAL)
    Q_PROPERTY(bool nextMatchLive READ nextMatchLive NOTIFY contentChanged FINAL)
    Q_PROPERTY(QString nextMatchDate READ nextMatchDate NOTIFY contentChanged FINAL) // ISO
    Q_PROPERTY(QString nextMatchDateText READ nextMatchDateText NOTIFY contentChanged FINAL)
    Q_PROPERTY(QString nextMatchOpponent READ nextMatchOpponent NOTIFY contentChanged FINAL)
    // Venue, competition and kick-off.
    Q_PROPERTY(QString nextMatchDetails READ nextMatchDetails NOTIFY contentChanged FINAL)
    Q_PROPERTY(int daysToNextMatch READ daysToNextMatch NOTIFY contentChanged FINAL)
    // "Today", "Tomorrow", "In 5 days" or "Match in progress"; empty without a match.
    Q_PROPERTY(QString daysText READ daysText NOTIFY contentChanged FINAL)
    // The section the chip and the card open: prematch, or matchday for a live match.
    Q_PROPERTY(QString nextMatchSection READ nextMatchSection NOTIFY contentChanged FINAL)
    Q_PROPERTY(bool lineupSaved READ lineupSaved NOTIFY contentChanged FINAL)
    // Saved and without errors; warnings may remain.
    Q_PROPERTY(bool lineupReady READ lineupReady NOTIFY contentChanged FINAL)
    // Errors and warnings of the lineup validator, with the load statuses once they are read.
    Q_PROPERTY(int lineupIssueCount READ lineupIssueCount NOTIFY contentChanged FINAL)
    Q_PROPERTY(QString lineupText READ lineupText NOTIFY contentChanged FINAL)
    // ok, warn or danger (the StatusBadge kinds), shown with lineupText.
    Q_PROPERTY(QString lineupStatusKey READ lineupStatusKey NOTIFY contentChanged FINAL)
    Q_PROPERTY(QVariantMap nextMatchEmpty READ nextMatchEmpty NOTIFY contentChanged FINAL)

    // playerId, name, statusKey (PlayerStatusBadge), typeText, returnText, description (empty
    // without ViewAvailabilityDetails).
    Q_PROPERTY(QVariantList unavailable READ unavailable NOTIFY contentChanged FINAL)
    Q_PROPERTY(QVariantMap unavailableEmpty READ unavailableEmpty NOTIFY contentChanged FINAL)

    // Red and yellow players, red first: playerId, name, statusKey (LoadStatusBadge), statusText,
    // explanation ("A signal to talk to the player").
    Q_PROPERTY(QVariantList loadSignals READ loadSignals NOTIFY contentChanged FINAL)
    Q_PROPERTY(QVariantMap loadSignalsEmpty READ loadSignalsEmpty NOTIFY contentChanged FINAL)

    // The last 5 finished matches, the most recent first: matchId, resultKey (win, draw, loss or
    // none), letter, resultText, scoreText, opponent, dateText, ratingText.
    Q_PROPERTY(QVariantList form READ form NOTIFY contentChanged FINAL)
    // Goals for and against and the average rating of those matches.
    Q_PROPERTY(QString formSummary READ formSummary NOTIFY contentChanged FINAL)

    // sessionId, timeText, typeText, durationText.
    Q_PROPERTY(QVariantList todaySessions READ todaySessions NOTIFY contentChanged FINAL)
    Q_PROPERTY(QVariantMap todayEmpty READ todayEmpty NOTIFY contentChanged FINAL)

public:
    explicit DashboardViewModel(QObject* parent = nullptr);

    [[nodiscard]] qint64 teamId() const;
    void setTeamId(qint64 teamId);
    [[nodiscard]] int yellowCardThreshold() const;
    void setYellowCardThreshold(int threshold);
    [[nodiscard]] bool busy() const;
    [[nodiscard]] bool ready() const;
    [[nodiscard]] QString readError() const;
    [[nodiscard]] QString loadError() const;

    [[nodiscard]] bool hasNextMatch() const;
    [[nodiscard]] qint64 nextMatchId() const;
    [[nodiscard]] bool nextMatchLive() const;
    [[nodiscard]] QString nextMatchDate() const;
    [[nodiscard]] QString nextMatchDateText() const;
    [[nodiscard]] QString nextMatchOpponent() const;
    [[nodiscard]] QString nextMatchDetails() const;
    [[nodiscard]] int daysToNextMatch() const;
    [[nodiscard]] QString daysText() const;
    [[nodiscard]] QString nextMatchSection() const;
    [[nodiscard]] bool lineupSaved() const;
    [[nodiscard]] bool lineupReady() const;
    [[nodiscard]] int lineupIssueCount() const;
    [[nodiscard]] QString lineupText() const;
    [[nodiscard]] QString lineupStatusKey() const;
    [[nodiscard]] QVariantMap nextMatchEmpty() const;
    [[nodiscard]] QVariantList unavailable() const;
    [[nodiscard]] QVariantMap unavailableEmpty() const;
    [[nodiscard]] QVariantList loadSignals() const;
    [[nodiscard]] QVariantMap loadSignalsEmpty() const;
    [[nodiscard]] QVariantList form() const;
    [[nodiscard]] QString formSummary() const;
    [[nodiscard]] QVariantList todaySessions() const;
    [[nodiscard]] QVariantMap todayEmpty() const;

    // Reads everything anew; the load signals follow from the thread pool. A failed read is
    // reported; a failed load read clears only the load-signals card.
    Q_INVOKABLE void reload();

signals:
    void teamIdChanged();
    void yellowCardThresholdChanged();
    void busyChanged();
    void contentChanged();
    // A translated text for the user; the view shows it in a toast.
    void errorOccurred(const QString& message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void clear();
    void setBusy(bool busy);
    [[nodiscard]] const services::NextMatchInfo* next() const;
    [[nodiscard]] int countIssues(bool errorsOnly) const;
    [[nodiscard]] QVariantMap emptyState(const QString& text, const QString& actionText,
                                         const QString& section) const;

    services::DashboardService& m_dashboard;
    services::LoadService& m_load;
    services::LineupService& m_lineups;
    services::Session& m_session;
    qint64 m_teamId = 0;
    int m_yellowCardThreshold;
    bool m_busy = false;
    int m_request = 0;
    std::optional<core::Error> m_readFailure;
    std::optional<core::Error> m_loadFailure;
    std::optional<services::DashboardOverview> m_overview;
    std::optional<services::TeamLoad> m_teamLoad;
};

} // namespace fdss::viewmodels
