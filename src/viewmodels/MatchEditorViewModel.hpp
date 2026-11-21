#pragma once

#include "core/domain/Match.hpp"
#include "core/util/Error.hpp"
#include "viewmodels/OpponentListModel.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <map>
#include <string>

namespace fdss::services {
class CalendarService;
class Session;
} // namespace fdss::services

namespace fdss::viewmodels {

// The form of the add/edit match dialog, and adding an opponent from inside it.
class MatchEditorViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // The team of a new match. The view binds it to AppState.teamId.
    Q_PROPERTY(qint64 teamId READ teamId WRITE setTeamId NOTIFY teamIdChanged FINAL)
    Q_PROPERTY(bool adding READ adding NOTIFY fieldsChanged FINAL)
    // Without EditCalendar, or when the match is no longer planned.
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY fieldsChanged FINAL)
    // 0: adding.
    Q_PROPERTY(qint64 matchId READ matchId NOTIFY fieldsChanged FINAL)
    // 0: none chosen.
    Q_PROPERTY(qint64 opponentId READ opponentId WRITE setOpponentId NOTIFY fieldsChanged FINAL)
    // The name of the chosen opponent, for the toast after a delete.
    Q_PROPERTY(QString opponentName READ opponentName NOTIFY fieldsChanged FINAL)
    // yyyy-MM-dd.
    Q_PROPERTY(QString date READ date WRITE setDate NOTIFY fieldsChanged FINAL)
    // HH:mm, or empty.
    Q_PROPERTY(QString kickoffTime READ kickoffTime WRITE setKickoffTime NOTIFY fieldsChanged FINAL)
    // The database text of a core Venue.
    Q_PROPERTY(QString venue READ venue WRITE setVenue NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QString competition READ competition WRITE setCompetition NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged FINAL)
    // The date is before today: a planned match is filled in after it was played (packet P14).
    Q_PROPERTY(bool beforeToday READ beforeToday NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(fdss::viewmodels::OpponentListModel* opponents READ opponents CONSTANT FINAL)
    // The most recent first.
    Q_PROPERTY(QStringList competitionSuggestions READ competitionSuggestions NOTIFY
                   suggestionsChanged FINAL)
    // [{ value, text }]
    Q_PROPERTY(QVariantList venueOptions READ venueOptions NOTIFY optionsChanged FINAL)
    Q_PROPERTY(QString opponentError READ opponentError NOTIFY errorsChanged FINAL)
    Q_PROPERTY(QString newOpponentError READ newOpponentError NOTIFY errorsChanged FINAL)
    Q_PROPERTY(QString dateError READ dateError NOTIFY errorsChanged FINAL)
    Q_PROPERTY(QString kickoffTimeError READ kickoffTimeError NOTIFY errorsChanged FINAL)

public:
    explicit MatchEditorViewModel(QObject* parent = nullptr);

    [[nodiscard]] qint64 teamId() const;
    void setTeamId(qint64 teamId);
    [[nodiscard]] bool adding() const;
    [[nodiscard]] bool readOnly() const;
    [[nodiscard]] qint64 matchId() const;
    [[nodiscard]] qint64 opponentId() const;
    void setOpponentId(qint64 opponentId);
    [[nodiscard]] QString opponentName() const;
    [[nodiscard]] QString date() const;
    [[nodiscard]] bool beforeToday() const;
    void setDate(const QString& date);
    [[nodiscard]] QString kickoffTime() const;
    void setKickoffTime(const QString& time);
    [[nodiscard]] QString venue() const;
    void setVenue(const QString& venue);
    [[nodiscard]] QString competition() const;
    void setCompetition(const QString& competition);
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] OpponentListModel* opponents() const;
    [[nodiscard]] QStringList competitionSuggestions() const;
    [[nodiscard]] QVariantList venueOptions() const;
    [[nodiscard]] QString opponentError() const;
    [[nodiscard]] QString newOpponentError() const;
    [[nodiscard]] QString dateError() const;
    [[nodiscard]] QString kickoffTimeError() const;

    // A form with this date, the first venue and no errors.
    Q_INVOKABLE void startAdding(const QString& date);
    // False when the match cannot be read; errorOccurred says why.
    Q_INVOKABLE bool startEditing(qint64 matchId);
    // True when added; the new opponent becomes the chosen one. Otherwise newOpponentError or
    // errorOccurred says why.
    Q_INVOKABLE bool addOpponent(const QString& name);
    Q_INVOKABLE void clearNewOpponentError();
    // True when saved. Otherwise the error properties or errorOccurred say why.
    Q_INVOKABLE bool save();

signals:
    void teamIdChanged();
    void fieldsChanged();
    void statusTextChanged();
    void suggestionsChanged();
    void optionsChanged();
    void errorsChanged();
    void saved(qint64 matchId, const QString& date);
    // A translated text for the user; the view shows it in a toast.
    void errorOccurred(const QString& message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void refreshChoices();
    void clearError(const std::string& field);
    [[nodiscard]] QString errorText(const std::string& field) const;
    void addFieldError(const std::string& field);
    void report(const core::Error& error);

    services::CalendarService& m_calendar;
    services::Session& m_session;
    OpponentListModel* m_opponents;

    qint64 m_teamId = 0;
    qint64 m_matchId = 0;
    qint64 m_opponentId = 0;
    QString m_date;
    QString m_kickoffTime;
    core::Venue m_venue = core::Venue::Home;
    QString m_competition;
    core::MatchStatus m_status = core::MatchStatus::Planned;
    QStringList m_competitionSuggestions;
    // By column name. Kept as errors, not as text, so the messages follow the language.
    std::map<std::string, core::Error> m_errors;
};

} // namespace fdss::viewmodels
