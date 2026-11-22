#pragma once

#include "core/algorithms/Load.hpp"

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <set>
#include <string>

namespace fdss::services {
class LoadService;
class Session;
} // namespace fdss::services

namespace fdss::viewmodels {

// The load signal thresholds of SPEC section 7.1, changed in Settings by a role with
// ManageSettings (P19): the three ratios and the monotony. They are kept on this computer, like
// the yellow-card threshold, and ServiceRegistry hands them to LoadService at the start.
class LoadSettingsViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // In the format of the app language.
    Q_PROPERTY(QString ratioHigh READ ratioHigh NOTIFY thresholdsChanged FINAL)
    Q_PROPERTY(QString ratioElevated READ ratioElevated NOTIFY thresholdsChanged FINAL)
    Q_PROPERTY(QString ratioLow READ ratioLow NOTIFY thresholdsChanged FINAL)
    Q_PROPERTY(QString monotonyHigh READ monotonyHigh NOTIFY thresholdsChanged FINAL)
    // The defaults of SPEC section 7.1 are in use.
    Q_PROPERTY(bool isDefault READ isDefault NOTIFY thresholdsChanged FINAL)
    Q_PROPERTY(bool canEdit READ canEdit CONSTANT FINAL)
    Q_PROPERTY(QString ratioHighError READ ratioHighError NOTIFY errorsChanged FINAL)
    Q_PROPERTY(QString ratioElevatedError READ ratioElevatedError NOTIFY errorsChanged FINAL)
    Q_PROPERTY(QString ratioLowError READ ratioLowError NOTIFY errorsChanged FINAL)
    Q_PROPERTY(QString monotonyHighError READ monotonyHighError NOTIFY errorsChanged FINAL)

public:
    explicit LoadSettingsViewModel(QObject* parent = nullptr);

    [[nodiscard]] QString ratioHigh() const;
    [[nodiscard]] QString ratioElevated() const;
    [[nodiscard]] QString ratioLow() const;
    [[nodiscard]] QString monotonyHigh() const;
    [[nodiscard]] bool isDefault() const;
    [[nodiscard]] bool canEdit() const;
    [[nodiscard]] QString ratioHighError() const;
    [[nodiscard]] QString ratioElevatedError() const;
    [[nodiscard]] QString ratioLowError() const;
    [[nodiscard]] QString monotonyHighError() const;

    // The texts take a decimal point or a decimal comma. True when stored; otherwise the error
    // properties or errorOccurred say why, and nothing changes.
    Q_INVOKABLE bool save(const QString& ratioHigh, const QString& ratioElevated,
                          const QString& ratioLow, const QString& monotonyHigh);
    Q_INVOKABLE bool restoreDefaults();
    Q_INVOKABLE void clearErrors();

    // What was stored on this computer; the defaults for what is missing.
    [[nodiscard]] static core::LoadThresholds storedThresholds();

signals:
    void thresholdsChanged();
    void errorsChanged();
    // A translated text for the user; the view shows it in a toast.
    void errorOccurred(const QString& message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    [[nodiscard]] bool apply(const core::LoadThresholds& thresholds);
    [[nodiscard]] QString errorText(const std::string& field) const;

    services::LoadService& m_load;
    services::Session& m_session;
    // Fields with an error, kept as names so the messages follow the language.
    std::set<std::string> m_errors;
};

} // namespace fdss::viewmodels
