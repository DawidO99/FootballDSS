#pragma once

#include "core/domain/Attendance.hpp"
#include "core/util/Error.hpp"
#include "viewmodels/SessionPickerModel.hpp"

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QString>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>
#include <vector>

namespace fdss::services {
class Clock;
class Session;
class TrainingService;
struct AttendanceSheet;
} // namespace fdss::services

namespace fdss::viewmodels {

// The training sheet: one row per player of a session, what the keyboard does to them, and the
// save. Saving and reading the same session again emit dataChanged instead of a reset, so the
// delegates and the focus in the grid stay.
class TrainingSessionViewModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    // 0: no team, and no sessions. The view binds it to AppState.teamId.
    Q_PROPERTY(qint64 teamId READ teamId WRITE setTeamId NOTIFY teamIdChanged FINAL)
    Q_PROPERTY(fdss::viewmodels::SessionPickerModel* sessions READ sessions CONSTANT FINAL)
    // 0: no session is open.
    Q_PROPERTY(qint64 sessionId READ sessionId NOTIFY sessionChanged FINAL)
    Q_PROPERTY(int durationMin READ durationMin NOTIFY sessionChanged FINAL)
    Q_PROPERTY(bool hasSessions READ hasSessions NOTIFY sessionsChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY sessionChanged FINAL)
    // Without EditTraining.
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY sessionChanged FINAL)
    // The user changed something, so leaving must ask.
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged FINAL)
    // There is something to save, also with nothing changed: a row with no saved attendance holds a
    // default, and without this "everyone present" could not be saved at all.
    Q_PROPERTY(bool canSave READ canSave NOTIFY dirtyChanged FINAL)
    // Rows that a save would write: with no saved attendance, or changed since it was saved.
    Q_PROPERTY(int unsavedCount READ unsavedCount NOTIFY unsavedChanged FINAL)
    // How many, in words; empty when none.
    Q_PROPERTY(QString unsavedText READ unsavedText NOTIFY unsavedChanged FINAL)
    Q_PROPERTY(int maxRpe READ maxRpe CONSTANT FINAL)
    Q_PROPERTY(int maxMinutes READ maxMinutes CONSTANT FINAL)
    // [{ value, text }] for the status of a row.
    Q_PROPERTY(QVariantList statusOptions READ statusOptions NOTIFY optionsChanged FINAL)
    // Why the sheet could not be read; the view offers to try again. Empty: it is fine.
    Q_PROPERTY(QString loadError READ loadError NOTIFY loadErrorChanged FINAL)

public:
    // QML uses the role names from roleNames(); the enum is for C++ callers.
    enum DataRole {
        PlayerIdRole = Qt::UserRole + 1,
        ShirtNumberRole, // int or nothing
        PlayerNameRole,
        StatusRole, // the database text
        RpeRole,    // int or nothing
        // The override or the duration of the session; nothing unless present. A value out of range
        // stays as it was typed, with the error beside it.
        MinutesRole,
        LoadRole,         // RPE × minutes; nothing without an RPE or with minutes out of range
        CellsEnabledRole, // RPE and minutes can be edited: present, and the role allows it
        RowErrorRole,     // translated, empty when the row is fine
        MinutesHasErrorRole
    };

    enum Column { StatusColumn, RpeColumn, MinutesColumn };
    Q_ENUM(Column)

    explicit TrainingSessionViewModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] qint64 teamId() const;
    void setTeamId(qint64 teamId);
    [[nodiscard]] SessionPickerModel* sessions() const;
    [[nodiscard]] qint64 sessionId() const;
    [[nodiscard]] int durationMin() const;
    [[nodiscard]] bool hasSessions() const;
    [[nodiscard]] int count() const;
    [[nodiscard]] bool readOnly() const;
    [[nodiscard]] bool dirty() const;
    [[nodiscard]] bool canSave() const;
    [[nodiscard]] int unsavedCount() const;
    [[nodiscard]] QString unsavedText() const;
    [[nodiscard]] int maxRpe() const;
    [[nodiscard]] int maxMinutes() const;
    [[nodiscard]] QVariantList statusOptions() const;
    [[nodiscard]] QString loadError() const;

    // Reads the sessions and the open sheet again. Rows with changes of the user stay as they are.
    Q_INVOKABLE void reload();
    // Drops changes that were not saved. A session that does not exist: errorOccurred, and the
    // default session.
    Q_INVOKABLE void openSession(qint64 sessionId);
    // The most recent session on or before today, see SessionPickerModel::defaultSessionId().
    Q_INVOKABLE void openDefaultSession();
    // A status other than present clears the RPE and the minutes.
    Q_INVOKABLE void setStatus(int row, const QString& status);
    // Digit 0-9. `1` then `0` within kTenWindowMs in the same row is 10.
    Q_INVOKABLE void enterRpeDigit(int row, int digit);
    Q_INVOKABLE void clearRpe(int row);
    // Digits only; empty means the duration of the session. A value out of range is kept and shown
    // as an error in the row, because a save must not write something else than what is on the
    // screen.
    Q_INVOKABLE void setMinutesText(int row, const QString& text);
    // A cell got the focus: this is what revertCell() goes back to.
    Q_INVOKABLE void rememberCell(int row, int column);
    // Esc. On a status it also brings back the RPE and the minutes that the change has cleared.
    Q_INVOKABLE void revertCell(int row, int column);
    // Leaves the players whom the availability keeps out, see AttendanceSheetRow::defaultStatus.
    Q_INVOKABLE void markAllPresent();
    // True when saved. Otherwise the rows or errorOccurred say why.
    Q_INVOKABLE bool save();
    Q_INVOKABLE void discardChanges();
    // The first row with an error, -1 when there is none.
    Q_INVOKABLE int firstErrorRow() const;

    // enterRpeDigit() with the time given, for the tests: Clock only has whole seconds.
    void enterRpeDigitAt(int row, int digit, qint64 elapsedMs);

    static constexpr qint64 kTenWindowMs = 600;

signals:
    void teamIdChanged();
    void sessionChanged();
    void sessionsChanged();
    void dirtyChanged();
    void unsavedChanged();
    void optionsChanged();
    void loadErrorChanged();
    void saved();
    // A translated text for the user; the view shows it in a toast.
    void errorOccurred(const QString& message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Row {
        core::PlayerId playerId;
        std::optional<int> shirtNumber;
        QString name;
        core::AttendanceDraft draft;
        core::AttendanceStatus defaultStatus = core::AttendanceStatus::Present;
        // The database holds a row for this player.
        bool saved = false;
        // Kept as an error, not as text, so the message follows the language.
        std::optional<core::Error> saveError;
    };

    struct RememberedCell {
        int row = -1;
        int column = -1;
        core::AttendanceDraft draft;
    };

    struct LastOne {
        int row = -1;
        qint64 elapsedMs = 0;
    };

    [[nodiscard]] bool isEditable(int row) const;
    [[nodiscard]] bool loadSheet(qint64 sessionId, core::Error& failure);
    void applySheet(services::AttendanceSheet sheet);
    void clearSheet();
    void refreshSessions();
    void setRpe(int row, std::optional<int> rpe);
    void afterRowEdit(int row);
    void updateDirty();
    void setLoadFailure(std::optional<core::Error> failure);
    [[nodiscard]] QString rowError(const Row& row) const;
    [[nodiscard]] bool minutesHaveError(const Row& row) const;
    [[nodiscard]] std::optional<int> minutesOf(const Row& row) const;
    // Without EditTraining: errorOccurred, and the service is not called. The service checks the
    // same again (AGENTS section 4).
    [[nodiscard]] bool checkAllowed();
    void report(const core::Error& error);

    services::TrainingService& m_training;
    services::Session& m_session;
    const services::Clock& m_clock;
    SessionPickerModel* m_sessions;

    qint64 m_teamId = 0;
    qint64 m_sessionId = 0;
    int m_durationMin = 0;
    std::vector<Row> m_rows;
    std::vector<core::AttendanceDraft> m_baseline;
    bool m_dirty = false;
    bool m_canSave = false;
    int m_unsavedCount = 0;
    std::optional<RememberedCell> m_remembered;
    std::optional<LastOne> m_lastOne;
    std::optional<core::Error> m_loadFailure;
    QElapsedTimer m_digitTimer;
};

} // namespace fdss::viewmodels
