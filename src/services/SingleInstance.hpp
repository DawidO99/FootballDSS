#pragma once

#include "core/util/Error.hpp"

#include <QLockFile>
#include <QString>

#include <memory>

namespace fdss::services {

// Holds <directory>/FootballDSS.lock while the app runs, so a second start can tell (P19). The
// lock is released by the destructor; a lock left by a process that died is taken over.
class SingleInstance {
    // Only acquire() constructs one, through std::make_unique.
    struct Key {
        explicit Key() = default;
    };

public:
    // Conflict: another running FootballDSS holds the lock. Io: the lock file cannot be written.
    [[nodiscard]] static core::Result<std::unique_ptr<SingleInstance>>
    acquire(const QString& directory);

    SingleInstance(Key key, const QString& path);
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;
    ~SingleInstance() = default;

private:
    QLockFile m_lock;
};

} // namespace fdss::services
