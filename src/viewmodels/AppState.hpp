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
    // Temporary, for the test club button in Main.qml. P07b removes it.
    Q_PROPERTY(bool debugBuild READ debugBuild CONSTANT FINAL)

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
    [[nodiscard]] bool debugBuild() const;

    // `permission` is the name of a core Permission. An unknown name is reported and gives false.
    // A binding on this does not update by itself. It holds in a section view because Main.qml
    // builds those anew for every login (AGENTS section 6).
    Q_INVOKABLE bool can(const QString& permission) const;
    Q_INVOKABLE void logout();
    // Re-reads the state after setup, login or a change of staff. When the database cannot be read,
    // emits errorOccurred and keeps the state: a failed read does not mean that there is no club.
    Q_INVOKABLE void refresh();
    // Temporary, debug builds only, PIN 1234: lets P08 and P09 be clicked through before the
    // screens of P07b exist. P07b removes it.
    Q_INVOKABLE void createTestClubAndLogin();

    [[nodiscard]] static QString roleDisplayName(core::Role role);

signals:
    void stateChanged();
    void userChanged();
    void clubChanged();
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
};

} // namespace fdss::viewmodels
