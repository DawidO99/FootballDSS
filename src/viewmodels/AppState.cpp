#include "viewmodels/AppState.hpp"

#include "core/domain/Permissions.hpp"
#include "services/AuthService.hpp"
#include "services/Session.hpp"
#include "services/SetupService.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/ServiceRegistry.hpp"

#include <QCoreApplication>
#include <QEvent>

#include <algorithm>

namespace fdss::viewmodels {

AppState::AppState(QObject* parent)
    : QObject(parent), m_auth(ServiceRegistry::instance().auth()),
      m_session(ServiceRegistry::instance().session()),
      m_setup(ServiceRegistry::instance().setup()) {
    // Installing a translator sends QEvent::LanguageChange to the application object only.
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

bool AppState::debugBuild() const {
#ifdef QT_NO_DEBUG
    return false;
#else
    return true;
#endif
}

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
    const auto complete = m_setup.isSetupComplete();
    if (!complete) {
        report(complete.error());
        return;
    }

    QString clubName;
    QString teamName;
    qint64 teamId = 0;
    if (*complete) {
        const auto context = m_setup.clubContext();
        if (!context) {
            report(context.error());
            return;
        }
        clubName = QString::fromStdString(context->club.name);
        teamName = QString::fromStdString(context->team.name);
        teamId = context->team.id.value;
    }
    if (clubName != m_clubName || teamName != m_teamName || teamId != m_teamId) {
        m_clubName = clubName;
        m_teamName = teamName;
        m_teamId = teamId;
        emit clubChanged();
    }

    const auto& user = m_session.user();
    const QString userName = user ? QString::fromStdString(user->name) : QString{};
    const std::optional<core::Role> role =
        user ? std::optional<core::Role>(user->role) : std::nullopt;
    if (userName != m_userName || role != m_role) {
        m_userName = userName;
        m_role = role;
        emit userChanged();
    }

    // Last: whatever a new state brings up reads the user and the club above.
    const State state = !*complete ? State::NeedsSetup : user ? State::Ready : State::NeedsLogin;
    if (state != m_state) {
        m_state = state;
        emit stateChanged();
    }
}

void AppState::createTestClubAndLogin() {
#ifdef QT_NO_DEBUG
    qWarning("AppState: the test club exists only in debug builds");
#else
    constexpr auto kPin = "1234";
    if (m_state == State::NeedsSetup) {
        core::ClubDraft club;
        club.name = "Test club";
        core::TeamDraft team;
        team.name = "Test team";
        team.season = "2026/27";
        if (const auto user = m_setup.completeSetup(club, team, "Test Coach", kPin); !user) {
            report(user.error());
            return;
        }
    } else if (m_state == State::NeedsLogin) {
        const auto staff = m_auth.listActiveStaff();
        if (!staff) {
            report(staff.error());
            return;
        }
        const auto coach = std::ranges::find(*staff, core::Role::Coach, &core::StaffUser::role);
        if (coach == staff->end()) {
            emit errorOccurred(tr("The test login needs a coach with PIN 1234."));
            return;
        }
        const auto check = m_auth.login(coach->id, kPin);
        if (!check) {
            report(check.error());
            return;
        }
        if (check->status != services::PinCheckStatus::Accepted) {
            emit errorOccurred(tr("The test login needs a coach with PIN 1234."));
            return;
        }
    }
    refresh();
#endif
}

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
    if (event->type() == QEvent::LanguageChange && m_role) {
        emit userChanged();
    }
    return QObject::eventFilter(watched, event);
}

void AppState::report(const core::Error& error) {
    qCritical("AppState: %s", error.detail.c_str());
    emit errorOccurred(ErrorPresenter::message(error));
}

} // namespace fdss::viewmodels
