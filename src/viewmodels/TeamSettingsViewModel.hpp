#pragma once

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace fdss::services {
class Session;
class SetupService;
} // namespace fdss::services

namespace fdss::viewmodels {

// The name and season of the team, changed in Settings by a role with ManageSettings (P19
// decision 2). The category stays as the setup wizard stored it.
class TeamSettingsViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString teamName READ teamName NOTIFY teamChanged FINAL)
    Q_PROPERTY(QString season READ season NOTIFY teamChanged FINAL)
    Q_PROPERTY(bool canEdit READ canEdit CONSTANT FINAL)
    Q_PROPERTY(QString nameError READ nameError NOTIFY errorsChanged FINAL)
    Q_PROPERTY(QString seasonError READ seasonError NOTIFY errorsChanged FINAL)

public:
    explicit TeamSettingsViewModel(QObject* parent = nullptr);

    [[nodiscard]] QString teamName() const;
    [[nodiscard]] QString season() const;
    [[nodiscard]] bool canEdit() const;
    [[nodiscard]] QString nameError() const;
    [[nodiscard]] QString seasonError() const;

    // True when stored; otherwise the error properties or errorOccurred say why. The view then
    // calls AppState.refresh(), which shows the new name in the top bar.
    Q_INVOKABLE bool save(const QString& name, const QString& season);
    Q_INVOKABLE void clearErrors();
    // Reads the team again; a failed read is reported and keeps the shown values.
    Q_INVOKABLE void reload();

signals:
    void teamChanged();
    void errorsChanged();
    // A translated text for the user; the view shows it in a toast.
    void errorOccurred(const QString& message);

private:
    services::SetupService& m_setup;
    services::Session& m_session;

    QString m_teamName;
    QString m_season;
    QString m_nameError;
    QString m_seasonError;
};

} // namespace fdss::viewmodels
