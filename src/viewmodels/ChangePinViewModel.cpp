#include "viewmodels/ChangePinViewModel.hpp"

#include "services/AuthService.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/ServiceRegistry.hpp"

namespace fdss::viewmodels {

ChangePinViewModel::ChangePinViewModel(QObject* parent)
    : QObject(parent), m_auth(ServiceRegistry::instance().auth()),
      m_lock(ServiceRegistry::instance().clock()) {
    connect(&m_lock, &LockCountdown::changed, this, &ChangePinViewModel::errorsChanged);
    connect(&m_lock, &LockCountdown::lockedChanged, this, &ChangePinViewModel::lockedChanged);
}

QString ChangePinViewModel::currentPinError() const {
    return m_lock.locked() ? m_lock.message() : m_currentPinError;
}

QString ChangePinViewModel::newPinError() const { return m_newPinError; }

QString ChangePinViewModel::pinRepeatError() const { return m_pinRepeatError; }

bool ChangePinViewModel::locked() const { return m_lock.locked(); }

bool ChangePinViewModel::change(const QString& currentPin, const QString& newPin,
                                const QString& pinRepeat) {
    clearErrors();
    if (m_lock.locked()) {
        return false;
    }
    if (!checkNewPin(newPin, pinRepeat)) {
        return false;
    }
    if (newPin == currentPin) {
        m_newPinError = ErrorPresenter::invalidInputMessage("pin_same");
        emit errorsChanged();
        return false;
    }

    const auto check = m_auth.changeOwnPin(currentPin.toStdString(), newPin.toStdString());
    if (!check) {
        // The new PIN is valid by now, so a malformed PIN can only be the current one.
        if (check.error().code == core::ErrorCode::InvalidInput) {
            m_currentPinError = ErrorPresenter::message(check.error());
            emit errorsChanged();
        } else {
            qCritical("ChangePinViewModel: %s", check.error().detail.c_str());
            emit errorOccurred(ErrorPresenter::message(check.error()));
        }
        return false;
    }
    switch (check->status) {
    case services::PinCheckStatus::Accepted:
        return true;
    case services::PinCheckStatus::WrongPin:
        m_currentPinError = ErrorPresenter::pinCheckMessage(*check);
        emit errorsChanged();
        break;
    case services::PinCheckStatus::LockedOut:
        m_lock.start(check->lockedFor);
        break;
    }
    return false;
}

bool ChangePinViewModel::setRequired(const QString& newPin, const QString& pinRepeat) {
    clearErrors();
    if (!checkNewPin(newPin, pinRepeat)) {
        return false;
    }
    const auto stored = m_auth.setRequiredPin(newPin.toStdString());
    if (!stored) {
        if (stored.error().code == core::ErrorCode::InvalidInput) {
            m_newPinError = ErrorPresenter::invalidInputMessage(stored.error().field);
            emit errorsChanged();
        } else {
            qCritical("ChangePinViewModel: %s", stored.error().detail.c_str());
            emit errorOccurred(ErrorPresenter::message(stored.error()));
        }
        return false;
    }
    return true;
}

bool ChangePinViewModel::checkNewPin(const QString& newPin, const QString& pinRepeat) {
    const bool newPinValid = services::AuthService::isValidPin(newPin.toStdString());
    if (newPinValid && newPin == pinRepeat) {
        return true;
    }
    m_newPinError = newPinValid ? QString{} : ErrorPresenter::invalidInputMessage("pin");
    m_pinRepeatError =
        newPin == pinRepeat ? QString{} : ErrorPresenter::invalidInputMessage("pin_repeat");
    emit errorsChanged();
    return false;
}

void ChangePinViewModel::clearErrors() {
    m_currentPinError.clear();
    m_newPinError.clear();
    m_pinRepeatError.clear();
    emit errorsChanged();
}

void ChangePinViewModel::tickLock() { m_lock.tick(); }

} // namespace fdss::viewmodels
