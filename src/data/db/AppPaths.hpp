#pragma once

#include "core/util/Error.hpp"

#include <QString>

namespace fdss::data::AppPaths {

// <AppDataLocation>/club.sqlite. Creates the folder if missing.
[[nodiscard]] core::Result<QString> defaultDatabasePath();

// <AppDataLocation>/backups. Creates the folder if missing.
[[nodiscard]] core::Result<QString> backupDirectory();

} // namespace fdss::data::AppPaths
