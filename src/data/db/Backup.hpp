#pragma once

#include "core/util/Error.hpp"

#include <QString>

#include <string_view>

class QDateTime;
class QSqlDatabase;

namespace fdss::data {

inline constexpr int kBackupsToKeep = 10;

// Writes <directory>/club-<yyyyMMdd-HHmmss>-<reason>.sqlite with VACUUM INTO and returns its path.
// Further backups of the same second get -2, -3..., always above the highest suffix of that second;
// above 999999 the call returns Internal and creates no file.
// The empty target file is created first, so calls running at the same time never pick the same
// file; on failure only that file is removed. Creates the directory if missing. The reason becomes
// part of a file name, so it must match [a-z0-9_]+ (otherwise InvalidInput with field "reason").
// Fails inside a transaction: VACUUM cannot run there.
[[nodiscard]] core::Result<QString> createBackup(const QSqlDatabase& db, const QString& directory,
                                                 std::string_view reason, const QDateTime& now);

// Keeps the newest `keep` backups (at least one) by (timestamp, suffix). `protectedPath`, the
// backup just made, is never deleted and counts as one of them. Deletes only files matching the
// backup name pattern, never anything else in that folder.
[[nodiscard]] core::Status pruneBackups(const QString& directory, const QString& protectedPath = {},
                                        int keep = kBackupsToKeep);

} // namespace fdss::data
