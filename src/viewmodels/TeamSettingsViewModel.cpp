#include "viewmodels/TeamSettingsViewModel.hpp"

#include "core/domain/Permissions.hpp"
#include "services/Session.hpp"
#include "services/SetupService.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/ServiceRegistry.hpp"

namespace fdss::viewmodels {

TeamSettingsViewModel::TeamSettingsViewModel(QObject* parent)
    : QObject(parent), m_setup(ServiceRegistry::instance().setup()),
      m_session(ServiceRegistry::instance().session()) {
    reload();
}

QString TeamSettingsViewModel::teamName() const { return m_teamName; }

QString TeamSettingsViewModel::season() const { return m_season; }

bool TeamSettingsViewModel::canEdit() const {
    return m_session.isAllowed(core::Permission::ManageSettings);
}

QString TeamSettingsViewModel::nameError() const { return m_nameError; }

QString TeamSettingsViewModel::seasonError() const { return m_seasonError; }

bool TeamSettingsViewModel::save(const QString& name, const QString& season) {
    clearErrors();
    if (const auto allowed = m_session.require(core::Permission::ManageSettings); !allowed) {
        emit errorOccurred(ErrorPresenter::message(allowed.error()));
        return false;
    }
    const auto saved = m_setup.updateTeam(name.toStdString(), season.toStdString());
    if (!saved) {
        const core::Error& error = saved.error();
        if (error.code == core::ErrorCode::InvalidInput &&
            (error.field == "team_name" || error.field == "season")) {
            (error.field == "season" ? m_seasonError : m_nameError) =
                ErrorPresenter::invalidInputMessage(error.field);
            emit errorsChanged();
        } else {
            qCritical("TeamSettingsViewModel: %s", error.detail.c_str());
            emit errorOccurred(ErrorPresenter::message(error));
        }
        return false;
    }
    reload();
    return true;
}

void TeamSettingsViewModel::clearErrors() {
    if (m_nameError.isEmpty() && m_seasonError.isEmpty()) {
        return;
    }
    m_nameError.clear();
    m_seasonError.clear();
    emit errorsChanged();
}

void TeamSettingsViewModel::reload() {
    const auto context = m_setup.clubContext();
    if (!context) {
        qCritical("TeamSettingsViewModel: %s", context.error().detail.c_str());
        emit errorOccurred(ErrorPresenter::message(context.error()));
        return;
    }
    const QString name = QString::fromStdString(context->team.name);
    const QString season = QString::fromStdString(context->team.season);
    if (name == m_teamName && season == m_season) {
        return;
    }
    m_teamName = name;
    m_season = season;
    emit teamChanged();
}

} // namespace fdss::viewmodels
