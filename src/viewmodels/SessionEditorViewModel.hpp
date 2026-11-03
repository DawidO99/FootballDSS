#pragma once

#include "core/domain/TrainingSession.hpp"
#include "core/util/Error.hpp"

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <map>
#include <string>

namespace fdss::services {
class CalendarService;
class Session;
} // namespace fdss::services

namespace fdss::viewmodels {

// The form of the add/edit training session dialog.
class SessionEditorViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // The team of a new session. The view binds it to AppState.teamId.
    Q_PROPERTY(qint64 teamId READ teamId WRITE setTeamId NOTIFY teamIdChanged FINAL)
    Q_PROPERTY(bool adding READ adding NOTIFY fieldsChanged FINAL)
    // Without EditCalendar.
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY fieldsChanged FINAL)
    // 0: adding.
    Q_PROPERTY(qint64 sessionId READ sessionId NOTIFY fieldsChanged FINAL)
    // The form differs from what was loaded.
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged FINAL)
    // yyyy-MM-dd.
    Q_PROPERTY(QString date READ date WRITE setDate NOTIFY fieldsChanged FINAL)
    // HH:mm, or empty.
    Q_PROPERTY(QString startTime READ startTime WRITE setStartTime NOTIFY fieldsChanged FINAL)
    // An int or null. A signal of its own: a NumberField shown a value again drops the text it has
    // rejected, so its value must not be read anew when another field changes.
    Q_PROPERTY(
        QVariant durationMin READ durationMin WRITE setDurationMin NOTIFY durationMinChanged FINAL)
    // The database text of a core SessionType.
    Q_PROPERTY(QString type READ type WRITE setType NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QString note READ note WRITE setNote NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(int minDuration READ minDuration CONSTANT FINAL)
    Q_PROPERTY(int maxDuration READ maxDuration CONSTANT FINAL)
    Q_PROPERTY(int durationStep READ durationStep CONSTANT FINAL)
    // [{ value, text }]
    Q_PROPERTY(QVariantList typeOptions READ typeOptions NOTIFY optionsChanged FINAL)
    Q_PROPERTY(QString dateError READ dateError NOTIFY errorsChanged FINAL)
    Q_PROPERTY(QString startTimeError READ startTimeError NOTIFY errorsChanged FINAL)
    Q_PROPERTY(QString durationError READ durationError NOTIFY errorsChanged FINAL)

public:
    explicit SessionEditorViewModel(QObject* parent = nullptr);

    [[nodiscard]] qint64 teamId() const;
    void setTeamId(qint64 teamId);
    [[nodiscard]] bool adding() const;
    [[nodiscard]] bool readOnly() const;
    [[nodiscard]] qint64 sessionId() const;
    [[nodiscard]] bool dirty() const;
    [[nodiscard]] QString date() const;
    void setDate(const QString& date);
    [[nodiscard]] QString startTime() const;
    void setStartTime(const QString& time);
    [[nodiscard]] QVariant durationMin() const;
    void setDurationMin(const QVariant& minutes);
    [[nodiscard]] QString type() const;
    void setType(const QString& type);
    [[nodiscard]] QString note() const;
    void setNote(const QString& note);
    [[nodiscard]] int minDuration() const;
    [[nodiscard]] int maxDuration() const;
    [[nodiscard]] int durationStep() const;
    [[nodiscard]] QVariantList typeOptions() const;
    [[nodiscard]] QString dateError() const;
    [[nodiscard]] QString startTimeError() const;
    [[nodiscard]] QString durationError() const;

    // A form with this date, the default duration and type, and no errors.
    Q_INVOKABLE void startAdding(const QString& date);
    // False when the session cannot be read; errorOccurred says why.
    Q_INVOKABLE bool startEditing(qint64 sessionId);
    // True when saved. Otherwise the error properties or errorOccurred say why.
    Q_INVOKABLE bool save();

signals:
    void teamIdChanged();
    void fieldsChanged();
    void durationMinChanged();
    void dirtyChanged();
    void optionsChanged();
    void errorsChanged();
    void saved(qint64 sessionId, const QString& date);
    // A translated text for the user; the view shows it in a toast.
    void errorOccurred(const QString& message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // What the form holds, as it holds it: text that does not parse is a change too.
    struct Fields {
        QString date;
        QString startTime;
        QVariant durationMin;
        core::SessionType type = core::SessionType::Training;
        QString note;

        friend bool operator==(const Fields&, const Fields&) = default;
    };

    void load(Fields fields, qint64 sessionId);
    void clearError(const std::string& field);
    [[nodiscard]] QString errorText(const std::string& field) const;
    void addFieldError(const std::string& field);
    void report(const core::Error& error);

    services::CalendarService& m_calendar;
    services::Session& m_session;

    qint64 m_teamId = 0;
    qint64 m_sessionId = 0;
    Fields m_fields;
    Fields m_loaded;
    // By column name. Kept as errors, not as text, so the messages follow the language.
    std::map<std::string, core::Error> m_errors;
};

} // namespace fdss::viewmodels
