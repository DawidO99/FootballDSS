#include "viewmodels/StaffViewModel.hpp"

#include "core/domain/Enums.hpp"
#include "core/domain/Permissions.hpp"
#include "core/domain/Validation.hpp"
#include "services/AuthService.hpp"
#include "services/Session.hpp"
#include "viewmodels/AppState.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/ServiceRegistry.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QVariantMap>

#include <utility>

namespace fdss::viewmodels {

StaffViewModel::StaffViewModel(QObject* parent)
    : QObject(parent), m_auth(ServiceRegistry::instance().auth()),
      m_session(ServiceRegistry::instance().session()), m_staff(new StaffListModel(this)) {
    if (const auto& user = m_session.user()) {
        m_currentUserId = user->id.value;
    }
    // Installing a translator sends QEvent::LanguageChange to the application object only.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
    reload();
}

StaffListModel* StaffViewModel::staff() const { return m_staff; }

QVariantList StaffViewModel::roleOptions() const {
    QVariantList options;
    for (const core::Role role : core::allValues<core::Role>()) {
        options.append(
            QVariantMap{{QStringLiteral("value"), QString::fromUtf8(core::toDbText(role))},
                        {QStringLiteral("text"), AppState::roleDisplayName(role)}});
    }
    return options;
}

qint64 StaffViewModel::currentUserId() const { return m_currentUserId; }

QString StaffViewModel::loadError() const { return m_loadError; }

QString StaffViewModel::nameError() const { return m_nameError; }

QString StaffViewModel::pinError() const { return m_pinError; }

QString StaffViewModel::pinRepeatError() const { return m_pinRepeatError; }

void StaffViewModel::reload() {
    if (!m_session.isAllowed(core::Permission::ManageStaff)) {
        return;
    }
    auto staff = m_auth.listAllStaff();
    const QString loadError = staff ? QString{} : ErrorPresenter::message(staff.error());
    if (staff) {
        m_staff->setStaff(std::move(*staff));
    } else {
        qCritical("StaffViewModel: %s", staff.error().detail.c_str());
        m_staff->setStaff({});
    }
    if (m_loadError != loadError) {
        m_loadError = loadError;
        emit loadErrorChanged();
    }
}

bool StaffViewModel::add(const QString& name, const QString& role, const QString& pin,
                         const QString& pinRepeat) {
    clearErrors();
    if (!checkAllowed()) {
        return false;
    }
    const bool nameValid = checkName(name);
    const bool pinValid = checkPin(pin, pinRepeat);
    if (!nameValid || !pinValid) {
        return false;
    }
    const auto parsedRole = core::fromDbText<core::Role>(role.toStdString());
    if (!parsedRole) {
        report(parsedRole.error());
        return false;
    }
    core::StaffUserDraft draft;
    draft.name = name.toStdString();
    draft.role = *parsedRole;
    const auto id = m_auth.addStaff(draft, pin.toStdString());
    return finish(id ? core::Status{} : std::unexpected(id.error()));
}

bool StaffViewModel::update(qint64 id, const QString& name, const QString& role) {
    clearErrors();
    if (!checkAllowed() || !checkName(name)) {
        return false;
    }
    const auto parsedRole = core::fromDbText<core::Role>(role.toStdString());
    if (!parsedRole) {
        report(parsedRole.error());
        return false;
    }
    const core::StaffUser* user = m_staff->find(id);
    if (user == nullptr) {
        report(core::Error{core::ErrorCode::NotFound, "the staff user is not in the list", {}});
        return false;
    }
    core::StaffUserDraft draft = *user;
    draft.name = name.toStdString();
    draft.role = *parsedRole;
    return finish(m_auth.updateStaff(core::StaffUserId{id}, draft));
}

bool StaffViewModel::resetPin(qint64 id, const QString& pin, const QString& pinRepeat) {
    clearErrors();
    if (!checkAllowed() || !checkPin(pin, pinRepeat)) {
        return false;
    }
    return finish(m_auth.resetPin(core::StaffUserId{id}, pin.toStdString()));
}

bool StaffViewModel::deactivate(qint64 id) {
    return checkAllowed() && finish(m_auth.deactivate(core::StaffUserId{id}));
}

bool StaffViewModel::reactivate(qint64 id) {
    return checkAllowed() && finish(m_auth.reactivate(core::StaffUserId{id}));
}

void StaffViewModel::clearErrors() {
    m_nameError.clear();
    m_pinError.clear();
    m_pinRepeatError.clear();
    emit errorsChanged();
}

bool StaffViewModel::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        emit roleOptionsChanged();
    }
    return QObject::eventFilter(watched, event);
}

bool StaffViewModel::checkAllowed() {
    if (m_session.isAllowed(core::Permission::ManageStaff)) {
        return true;
    }
    report(core::Error{
        core::ErrorCode::PermissionDenied, "refused by the viewmodel: ManageStaff is needed", {}});
    return false;
}

bool StaffViewModel::checkName(const QString& name) {
    core::StaffUserDraft draft;
    draft.name = name.toStdString();
    if (core::validate(draft).empty()) {
        return true;
    }
    m_nameError = ErrorPresenter::invalidInputMessage("name");
    emit errorsChanged();
    return false;
}

bool StaffViewModel::checkPin(const QString& pin, const QString& pinRepeat) {
    const bool pinValid = services::AuthService::isValidPin(pin.toStdString());
    m_pinError = pinValid ? QString{} : ErrorPresenter::invalidInputMessage("pin");
    m_pinRepeatError =
        pin == pinRepeat ? QString{} : ErrorPresenter::invalidInputMessage("pin_repeat");
    emit errorsChanged();
    return pinValid && pin == pinRepeat;
}

bool StaffViewModel::finish(const core::Status& status) {
    if (!status) {
        report(status.error());
        return false;
    }
    // Skips the list when this change has ended the session of the user.
    reload();
    emit staffChanged();
    return true;
}

void StaffViewModel::report(const core::Error& error) {
    // The last Coach is a rule the user ran into; anything else here is a real failure.
    if (error.code != core::ErrorCode::Conflict) {
        qCritical("StaffViewModel: %s", error.detail.c_str());
    }
    emit errorOccurred(ErrorPresenter::message(error));
}

} // namespace fdss::viewmodels
