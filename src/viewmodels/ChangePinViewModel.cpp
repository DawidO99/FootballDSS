#include "viewmodels/ChangePinViewModel.hpp"

#include "services/AuthService.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/ServiceRegistry.hpp"

namespace fdss::viewmodels {

ChangePinViewModel::ChangePinViewModel(QObject* parent)
    : QObject(parent), m_auth(ServiceRegistry::instance().auth()) {}

QString ChangePinViewModel::currentPinError() const { return m_currentPinError; }

QString ChangePinViewModel::newPinError() const { return m_newPinError; }

QString ChangePinViewModel::pinRepeatError() const { return m_pinRepeatError; }

bool ChangePinViewModel::change(const QString& currentPin, const QString& newPin,
                                const QString& pinRepeat) {
    clearErrors();
    const bool newPinValid = services::AuthService::isValidPin(newPin.toStdString());
    if (!newPinValid || newPin != pinRepeat) {
        m_newPinError = newPinValid ? QString{} : ErrorPresenter::invalidInputMessage("pin");
        m_pinRepeatError =
            newPin == pinRepeat ? QString{} : ErrorPresenter::invalidInputMessage("pin_repeat");
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
    if (check->status != services::PinCheckStatus::Accepted) {
        m_currentPinError = ErrorPresenter::pinCheckMessage(*check);
        emit errorsChanged();
        return false;
    }
    return true;
}

void ChangePinViewModel::clearErrors() {
    m_currentPinError.clear();
    m_newPinError.clear();
    m_pinRepeatError.clear();
    emit errorsChanged();
}

} // namespace fdss::viewmodels
