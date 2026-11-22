#pragma once

#include "core/util/Error.hpp"
#include "services/SingleInstance.hpp"

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <optional>

namespace fdss::viewmodels {

// What went wrong before the main window could load. main.cpp fills it through
// engine.singletonInstance<>() when the database cannot be opened (DatabaseErrorView shows it) or
// when the start is refused by lockInstance() (AlreadyRunningView).
class StartupViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged FINAL)
    Q_PROPERTY(QString databasePath READ databasePath NOTIFY errorChanged FINAL)
    // AlreadyRunningView (P19): true when another FootballDSS holds the lock; false when the lock
    // file cannot be written, which stops the start too.
    Q_PROPERTY(bool otherInstanceRunning READ otherInstanceRunning NOTIFY errorChanged FINAL)

public:
    explicit StartupViewModel(QObject* parent = nullptr);

    void setDatabaseError(const core::Error& error, const QString& databasePath);
    // Why the start was refused by lockInstance(); databasePath is for "Open data folder".
    void setInstanceError(const core::Error& error, const QString& databasePath);

    // The lock of the only running FootballDSS, in the folder of the database (P19). nullptr when
    // there is no such folder: the database error screen follows anyway. An error stops the start:
    // Conflict when another FootballDSS runs, Io when the lock file cannot be written, since then
    // nothing could tell a second start.
    [[nodiscard]] static core::Result<std::unique_ptr<services::SingleInstance>>
    lockInstance(const core::Result<QString>& databasePath);

    // Translated explanation with a next step, per error code (ErrorPresenter); never
    // Error::detail.
    [[nodiscard]] QString errorMessage() const;
    [[nodiscard]] QString databasePath() const;
    [[nodiscard]] bool otherInstanceRunning() const;

    Q_INVOKABLE void openDataFolder() const;

signals:
    void errorChanged();

private:
    std::optional<core::ErrorCode> m_errorCode;
    QString m_databasePath;
    bool m_otherInstanceRunning = true;
};

} // namespace fdss::viewmodels
