#pragma once

#include "core/util/Error.hpp"
#include "services/AuthService.hpp"
#include "viewmodels/LockCountdown.hpp"
#include "viewmodels/StaffListModel.hpp"

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace fdss::viewmodels {

// Who is logging in: the tiles of the active staff and the check of the PIN. Main.qml builds the
// view anew for every logout, so nothing of the last attempt stays behind. The language can change
// here, so the texts are made when they are read.
class ProfilePickerViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(fdss::viewmodels::StaffListModel* staff READ staff CONSTANT FINAL)
    // 0: nobody selected.
    Q_PROPERTY(qint64 selectedId READ selectedId NOTIFY selectionChanged FINAL)
    Q_PROPERTY(QString selectedName READ selectedName NOTIFY selectionChanged FINAL)
    // What the last attempt said, or the lockout with its countdown. Empty: nothing to say.
    Q_PROPERTY(QString message READ message NOTIFY messageChanged FINAL)
    Q_PROPERTY(bool locked READ locked NOTIFY messageChanged FINAL)
    // Why the list could not be read; the view offers to try again. Empty: the list is fine.
    Q_PROPERTY(QString loadError READ loadError NOTIFY loadErrorChanged FINAL)

public:
    explicit ProfilePickerViewModel(QObject* parent = nullptr);

    [[nodiscard]] StaffListModel* staff() const;
    [[nodiscard]] qint64 selectedId() const;
    [[nodiscard]] QString selectedName() const;
    [[nodiscard]] QString message() const;
    [[nodiscard]] bool locked() const;
    [[nodiscard]] QString loadError() const;

    // A single user is selected right away. A selection that left the list is dropped.
    Q_INVOKABLE void reload();
    // Shows a lockout that survived a restart. 0 clears the selection.
    Q_INVOKABLE void select(qint64 id);
    Q_INVOKABLE void login(const QString& pin);

    // One tick of the lockout countdown; public for tests.
    void tickLock();

signals:
    void selectionChanged();
    void messageChanged();
    void loadErrorChanged();
    // The session has started; the view calls AppState.refresh().
    void loggedIn();
    // The PIN was wrong or the user is locked; the view clears the field.
    void pinRejected();
    // A translated text for the user; the view shows it in a toast.
    void errorOccurred(const QString& message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // The time left as the database has it, when a profile is selected.
    void readLock();
    void forgetAttempt();
    void report(const core::Error& error);

    services::AuthService& m_auth;
    LockCountdown m_lock;
    StaffListModel* m_staff;

    qint64 m_selectedId = 0;
    std::optional<services::PinCheck> m_wrongPin;
    bool m_malformedPin = false;
    std::optional<core::Error> m_loadFailure;
};

} // namespace fdss::viewmodels
