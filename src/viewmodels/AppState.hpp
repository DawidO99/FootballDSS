#pragma once

#include "core/domain/Enums.hpp"
#include "core/util/Error.hpp"

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace fdss::services {
class AuthService;
class Session;
class SetupService;
} // namespace fdss::services

namespace fdss::viewmodels {

// Where the app stands and who is using it. Takes its services from ServiceRegistry, so it cannot
// be created before install(), and the QML engine holding it has to go before reset().
class AppState : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(State state READ state NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString userName READ userName NOTIFY userChanged FINAL)
    Q_PROPERTY(QString roleName READ roleName NOTIFY userChanged FINAL)
    Q_PROPERTY(QString clubName READ clubName NOTIFY clubChanged FINAL)
    Q_PROPERTY(QString teamName READ teamName NOTIFY clubChanged FINAL)
    Q_PROPERTY(qint64 teamId READ teamId NOTIFY clubChanged FINAL)
    // Changes with every login and logout, also when one user logs in over another and the state
    // stays Ready. Main.qml builds the shell anew on it.
    Q_PROPERTY(int sessionSerial READ sessionSerial NOTIFY sessionSerialChanged FINAL)

public:
    enum class State { NeedsSetup, NeedsLogin, Ready };
    Q_ENUM(State)

    explicit AppState(QObject* parent = nullptr);

    [[nodiscard]] State state() const;
    [[nodiscard]] QString userName() const;
    // Translated; follows the language of the app.
    [[nodiscard]] QString roleName() const;
    [[nodiscard]] QString clubName() const;
    [[nodiscard]] QString teamName() const;
    [[nodiscard]] qint64 teamId() const;
    [[nodiscard]] int sessionSerial() const;

    // `permission` is the name of a core Permission. An unknown name is reported and gives false.
    // A binding on this does not update by itself. It holds in a section view because Main.qml
    // builds those anew for every login (AGENTS section 6).
    Q_INVOKABLE bool can(const QString& permission) const;
    // Ends the session and leaves the shell, also when the database cannot be read then.
    Q_INVOKABLE void logout();
    // Re-reads the state after setup, login or a change of staff. When the database cannot be read,
    // emits errorOccurred and keeps the state, since a failed read does not mean that there is no
    // club; only Ready goes without a session, which lives in memory. All values are in place
    // before the first signal.
    Q_INVOKABLE void refresh();
    // For a message that the next session change would dismiss with every other toast, as the
    // first login after the setup does: errorOccurred brings it right after sessionSerialChanged.
    Q_INVOKABLE void deferError(const QString& message);

    [[nodiscard]] static QString roleDisplayName(core::Role role);

signals:
    void stateChanged();
    void userChanged();
    void clubChanged();
    void sessionSerialChanged();
    // A translated text for the user; Main.qml shows it in a toast.
    void errorOccurred(const QString& message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void report(const core::Error& error);

    services::AuthService& m_auth;
    services::Session& m_session;
    services::SetupService& m_setup;

    State m_state = State::NeedsSetup;
    QString m_userName;
    std::optional<core::Role> m_role;
    QString m_clubName;
    QString m_teamName;
    qint64 m_teamId = 0;
    int m_sessionSerial = 0;
    QString m_deferredError;
};

} // namespace fdss::viewmodels
