#pragma once

#include "core/util/Error.hpp"

#include <QString>
#include <QtGlobal>

#include <memory>

namespace fdss::viewmodels {

// Writes every Qt message (qDebug to qFatal) to <directory>/FootballDSS.log and hands it on to
// the handler installed before, so stderr keeps it too (P19). Every start begins a new file, as
// does a file that grows past `maxBytes`; the older ones are renamed FootballDSS.1.log,
// FootballDSS.2.log … and only the newest `keepFiles` files are kept. A file that cannot be moved
// away is continued, never emptied.
// main.cpp owns the one object; destroying it puts the previous handler back. A Qt message handler
// is process-wide (qInstallMessageHandler), so the open file and its mutex are a global of
// AppLog.cpp, the only one besides ServiceRegistry. It lives here with the composition root, where
// no service can reach it (P19 decision 13).
class AppLog {
    // Only install() constructs one, through std::make_unique.
    struct Key {
        explicit Key() = default;
    };

public:
    static constexpr int kKeepFiles = 5;
    static constexpr qint64 kMaxBytes = 5 * 1024 * 1024;

    // NewFile: the normal start. Append: adds to FootballDSS.log without rotating, for a start that
    // is refused and must not touch the files of an instance that may be running.
    enum class Start { NewFile, Append };

    // Io when the file cannot be opened; Internal while another AppLog exists.
    [[nodiscard]] static core::Result<std::unique_ptr<AppLog>>
    install(const QString& directory, int keepFiles = kKeepFiles, qint64 maxBytes = kMaxBytes,
            Start start = Start::NewFile);

    explicit AppLog(Key key);
    ~AppLog();
    AppLog(const AppLog&) = delete;
    AppLog& operator=(const AppLog&) = delete;
    AppLog(AppLog&&) = delete;
    AppLog& operator=(AppLog&&) = delete;

    // The file being written.
    [[nodiscard]] QString currentFile() const;
};

} // namespace fdss::viewmodels
