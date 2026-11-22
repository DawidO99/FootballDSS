#include "services/SingleInstance.hpp"

#include <QDir>

#include <format>
#include <memory>

namespace fdss::services {

SingleInstance::SingleInstance(Key /*key*/, const QString& path) : m_lock(path) {
    // A running instance holds the lock for as long as it likes; only a lock whose process is gone
    // counts as stale.
    m_lock.setStaleLockTime(0);
}

core::Result<std::unique_ptr<SingleInstance>> SingleInstance::acquire(const QString& directory) {
    const QString path = QDir(directory).filePath(QStringLiteral("FootballDSS.lock"));
    auto instance = std::make_unique<SingleInstance>(Key{}, path);
    if (instance->m_lock.tryLock(0)) {
        return instance;
    }
    if (instance->m_lock.error() == QLockFile::LockFailedError) {
        return std::unexpected(core::Error{core::ErrorCode::Conflict,
                                           "another FootballDSS holds the lock", "instance"});
    }
    return std::unexpected(core::Error{
        core::ErrorCode::Io, std::format("cannot write the lock file {}", path.toStdString()), {}});
}

} // namespace fdss::services
