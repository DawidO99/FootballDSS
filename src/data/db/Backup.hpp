#pragma once

#include "core/util/Error.hpp"

#include <QString>

#include <string_view>

class QDateTime;
class QSqlDatabase;

namespace fdss::data {

inline constexpr int kBackupsToKeep = 10;

// Writes <directory>/club-<yyyyMMdd-HHmmss>-<reason>.sqlite with VACUUM INTO and returns its path.
// Adds -2, -3... when the name is taken, because VACUUM INTO refuses an existing target. Creates
// the directory if missing. The reason becomes part of a file name, so it must match [a-z0-9_]+
// (otherwise InvalidInput with field "reason"). Fails inside a transaction: VACUUM cannot run
// there.
[[nodiscard]] core::Result<QString> createBackup(const QSqlDatabase& db, const QString& directory,
                                                 std::string_view reason, const QDateTime& now);

// Keeps the newest `keep` backups by (timestamp, suffix). Deletes only files matching the backup
// name pattern, never anything else in that folder.
[[nodiscard]] core::Status pruneBackups(const QString& directory, int keep = kBackupsToKeep);

} // namespace fdss::data
