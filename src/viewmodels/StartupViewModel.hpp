#pragma once

#include "core/util/Error.hpp"

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace fdss::viewmodels {

// What went wrong before the main window could load. main.cpp fills it through
// engine.singletonInstance<>() when the database cannot be opened; DatabaseErrorView shows it.
class StartupViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged FINAL)
    Q_PROPERTY(QString databasePath READ databasePath NOTIFY errorChanged FINAL)

public:
    explicit StartupViewModel(QObject* parent = nullptr);

    void setDatabaseError(const core::Error& error, const QString& databasePath);

    // Translated explanation with a next step, per error code; never Error::detail. Temporary home
    // of these texts: P07a moves them to ErrorPresenter.
    [[nodiscard]] QString errorMessage() const;
    [[nodiscard]] QString databasePath() const;

    Q_INVOKABLE void openDataFolder() const;

signals:
    void errorChanged();

private:
    std::optional<core::ErrorCode> m_errorCode;
    QString m_databasePath;
};

} // namespace fdss::viewmodels
