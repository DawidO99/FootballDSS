#pragma once

#include "core/util/Error.hpp"

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace fdss::services {
class AuthService;
} // namespace fdss::services

namespace fdss::viewmodels {

// A logged-in user changes their own PIN. Needs no permission, so it works for every role.
class ChangePinViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // Also says that the current PIN was wrong, or that the user is locked out.
    Q_PROPERTY(QString currentPinError READ currentPinError NOTIFY errorsChanged FINAL)
    Q_PROPERTY(QString newPinError READ newPinError NOTIFY errorsChanged FINAL)
    Q_PROPERTY(QString pinRepeatError READ pinRepeatError NOTIFY errorsChanged FINAL)

public:
    explicit ChangePinViewModel(QObject* parent = nullptr);

    [[nodiscard]] QString currentPinError() const;
    [[nodiscard]] QString newPinError() const;
    [[nodiscard]] QString pinRepeatError() const;

    // True when the PIN was changed; otherwise the error properties or errorOccurred say why.
    Q_INVOKABLE bool change(const QString& currentPin, const QString& newPin,
                            const QString& pinRepeat);
    Q_INVOKABLE void clearErrors();

signals:
    void errorsChanged();
    // A translated text for the user; the view shows it in a toast.
    void errorOccurred(const QString& message);

private:
    services::AuthService& m_auth;

    QString m_currentPinError;
    QString m_newPinError;
    QString m_pinRepeatError;
};

} // namespace fdss::viewmodels
