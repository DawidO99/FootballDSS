#pragma once

#include "core/util/Error.hpp"

#include <QString>
#include <QtGlobal>

namespace fdss::services {

// Writes every Qt message (qDebug to qFatal) to <directory>/FootballDSS.log and hands it on to
// the handler installed before, so stderr keeps it too (P19). Every install() starts a new file,
// as does a file that grows past `maxBytes`; the older ones are renamed FootballDSS.1.log,
// FootballDSS.2.log … and only the newest `keepFiles` files are kept.
// A Qt message handler is process-wide, so this is too: main.cpp installs it once.
class AppLog {
public:
    static constexpr int kKeepFiles = 5;
    static constexpr qint64 kMaxBytes = 5 * 1024 * 1024;

    [[nodiscard]] static core::Status install(const QString& directory, int keepFiles = kKeepFiles,
                                              qint64 maxBytes = kMaxBytes);
    // Puts the previous handler back and closes the file. Nothing happens when not installed.
    static void uninstall();
    // The file being written; empty when not installed.
    [[nodiscard]] static QString currentFile();
};

} // namespace fdss::services
