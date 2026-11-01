#include "viewmodels/StartupViewModel.hpp"

#include "viewmodels/ErrorPresenter.hpp"

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
    return m_errorCode ? ErrorPresenter::startupMessage(*m_errorCode) : QString{};
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
