#include "viewmodels/AppState.hpp"

#include "core/domain/Permissions.hpp"
#include "services/AuthService.hpp"
#include "services/Session.hpp"
#include "services/SetupService.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/ServiceRegistry.hpp"

#include <QCoreApplication>
#include <QEvent>

#include <optional>
#include <utility>

namespace fdss::viewmodels {

namespace {

// The club and the team; nullopt while the setup is not complete.
core::Result<std::optional<services::ClubContext>> readClub(const services::SetupService& setup) {
    const auto complete = setup.isSetupComplete();
    if (!complete) {
        return std::unexpected(complete.error());
    }
    if (!*complete) {
        return std::optional<services::ClubContext>{};
    }
    auto context = setup.clubContext();
    if (!context) {
        return std::unexpected(context.error());
    }
    return std::optional<services::ClubContext>(std::move(*context));
}

} // namespace

AppState::AppState(QObject* parent)
    : QObject(parent), m_auth(ServiceRegistry::instance().auth()),
      m_session(ServiceRegistry::instance().session()),
      m_setup(ServiceRegistry::instance().setup()) {
    // Installing a translator sends QEvent::LanguageChange to the application object, which only
    // an application-wide filter sees; eventFilter() ignores the copies Qt sends on to the items.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
    refresh();
}

AppState::State AppState::state() const { return m_state; }

QString AppState::userName() const { return m_userName; }

QString AppState::roleName() const { return m_role ? roleDisplayName(*m_role) : QString{}; }

QString AppState::clubName() const { return m_clubName; }

QString AppState::teamName() const { return m_teamName; }

qint64 AppState::teamId() const { return m_teamId; }

int AppState::sessionSerial() const { return m_sessionSerial; }

QString AppState::unavailableMessage() const { return m_unavailableMessage; }

bool AppState::can(const QString& permission) const {
    const auto parsed = core::permissionFromName(permission.toStdString());
    if (!parsed) {
        qWarning("AppState: unknown permission '%s'", qUtf8Printable(permission));
        return false;
    }
    return m_session.isAllowed(*parsed);
}

void AppState::logout() {
    m_auth.logout();
    refresh();
}

void AppState::refresh() {
    const auto club = readClub(m_setup);
    const auto& user = m_session.user();

    const State loggedIn = m_session.mustChangePin() ? State::NeedsPinChange : State::Ready;
    State state = m_state;
    if (club) {
        state = !*club ? State::NeedsSetup : user ? loggedIn : State::NeedsLogin;
    } else if (m_state == State::Ready || m_state == State::NeedsPinChange) {
        state = user ? loggedIn : State::NeedsLogin;
    }
    const QString unavailableMessage = state == State::Unavailable && !club
                                           ? ErrorPresenter::startupMessage(club.error().code)
                                           : QString{};
    const bool stateDiffers = state != m_state || unavailableMessage != m_unavailableMessage;
    m_state = state;
    m_unavailableMessage = unavailableMessage;

    const QString userName = user ? QString::fromStdString(user->name) : QString{};
    const std::optional<core::Role> role =
        user ? std::optional<core::Role>(user->role) : std::nullopt;
    const bool userDiffers = userName != m_userName || role != m_role;
    m_userName = userName;
    m_role = role;

    bool clubDiffers = false;
    if (club) {
        const QString clubName = *club ? QString::fromStdString((*club)->club.name) : QString{};
        const QString teamName = *club ? QString::fromStdString((*club)->team.name) : QString{};
        const qint64 teamId = *club ? (*club)->team.id.value : 0;
        clubDiffers = clubName != m_clubName || teamName != m_teamName || teamId != m_teamId;
        m_clubName = clubName;
        m_teamName = teamName;
        m_teamId = teamId;
    }

    const bool sessionDiffers = m_session.serial() != m_sessionSerial;
    m_sessionSerial = m_session.serial();

    // Whatever reacts to a signal reads the final values of all of them.
    if (userDiffers) {
        emit userChanged();
    }
    if (clubDiffers) {
        emit clubChanged();
    }
    if (stateDiffers) {
        emit stateChanged();
    }
    if (sessionDiffers) {
        emit sessionSerialChanged();
        if (!m_deferredError.isEmpty()) {
            emit errorOccurred(std::exchange(m_deferredError, QString{}));
        }
    }
    if (!club) {
        if (m_state == State::Unavailable) {
            // The screen of this state shows the message; a toast would repeat it.
            qCritical("AppState: %s", club.error().detail.c_str());
        } else {
            report(club.error());
        }
    }
}

void AppState::deferError(const QString& message) { m_deferredError = message; }

QString AppState::roleDisplayName(core::Role role) {
    switch (role) {
    case core::Role::Coach:
        return tr("Coach");
    case core::Role::Assistant:
        return tr("Assistant");
    case core::Role::Physio:
        return tr("Physio");
    case core::Role::Analyst:
        return tr("Analyst");
    }
    return {};
}

bool AppState::eventFilter(QObject* watched, QEvent* event) {
    if (watched == QCoreApplication::instance() && event->type() == QEvent::LanguageChange &&
        m_role) {
        emit userChanged();
    }
    return QObject::eventFilter(watched, event);
}

void AppState::report(const core::Error& error) {
    qCritical("AppState: %s", error.detail.c_str());
    emit errorOccurred(ErrorPresenter::message(error));
}

} // namespace fdss::viewmodels
