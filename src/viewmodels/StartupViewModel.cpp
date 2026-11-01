#include "viewmodels/StartupViewModel.hpp"

#include <QDesktopServices>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

namespace fdss::viewmodels {

StartupViewModel::StartupViewModel(QObject* parent) : QObject(parent) {}

void StartupViewModel::setDatabaseError(const core::Error& error, const QString& databasePath) {
    m_errorCode = error.code;
    m_databasePath = databasePath;
    emit errorChanged();
}

QString StartupViewModel::errorMessage() const {
    if (!m_errorCode) {
        return {};
    }
    switch (*m_errorCode) {
    case core::ErrorCode::NotADatabase:
        return tr("This file is not a FootballDSS database, or it is damaged. Close the app. Then "
                  "move the file out of the data folder to start with a new database, or replace "
                  "it with a copy from the backups folder. In both cases delete the files next to "
                  "it that end in -wal and -shm.");
    case core::ErrorCode::DatabaseLocked:
        return tr("Another program is using the database, possibly a second FootballDSS window. "
                  "Close it and start the app again.");
    case core::ErrorCode::IncompatibleVersion:
        return tr("This database was saved by a newer version of FootballDSS. Install the newer "
                  "version to open it. The file was not changed.");
    case core::ErrorCode::Io:
        return tr("The database file cannot be read or written. Check that the disk is not full "
                  "and that you may write to the data folder.");
    case core::ErrorCode::Internal:
        return tr("FootballDSS could not start its database module. Install the app again.");
    case core::ErrorCode::Database:
    case core::ErrorCode::NotFound:
    case core::ErrorCode::InvalidInput:
    case core::ErrorCode::Conflict:
    case core::ErrorCode::PermissionDenied:
    case core::ErrorCode::UnsupportedFormat:
        break;
    }
    return tr("The database could not be opened because of an unexpected error. Start the app "
              "again. If this repeats, close the app, replace the file with a copy from the "
              "backups folder and delete the files next to it that end in -wal and -shm.");
}

QString StartupViewModel::databasePath() const { return m_databasePath; }

void StartupViewModel::openDataFolder() const {
    // The path is empty when the data folder itself could not be created.
    const QString folder = m_databasePath.isEmpty()
                               ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                               : QFileInfo(m_databasePath).absolutePath();
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(folder))) {
        qWarning("StartupViewModel: cannot open folder '%s'", qUtf8Printable(folder));
    }
}

} // namespace fdss::viewmodels
