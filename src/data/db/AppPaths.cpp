#include "data/db/AppPaths.hpp"

#include <QDir>
#include <QStandardPaths>

#include <format>

namespace fdss::data::AppPaths {

namespace {

core::Result<QString> ensureDirectory(const QString& path) {
    if (!QDir().mkpath(path)) {
        return std::unexpected(core::Error{
            core::ErrorCode::Io, std::format("cannot create folder {}", path.toStdString()), {}});
    }
    return path;
}

core::Result<QString> appDataDirectory() {
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (path.isEmpty()) {
        return std::unexpected(
            core::Error{core::ErrorCode::Io, "no writable AppDataLocation on this system", {}});
    }
    return ensureDirectory(path);
}

} // namespace

core::Result<QString> defaultDatabasePath() {
    const core::Result<QString> directory = appDataDirectory();
    if (!directory) {
        return directory;
    }
    return QDir(*directory).filePath(QStringLiteral("club.sqlite"));
}

core::Result<QString> backupDirectory() {
    const core::Result<QString> directory = appDataDirectory();
    if (!directory) {
        return directory;
    }
    return ensureDirectory(QDir(*directory).filePath(QStringLiteral("backups")));
}

core::Result<QString> logDirectory() {
    const core::Result<QString> directory = appDataDirectory();
    if (!directory) {
        return directory;
    }
    return ensureDirectory(QDir(*directory).filePath(QStringLiteral("logs")));
}

} // namespace fdss::data::AppPaths
