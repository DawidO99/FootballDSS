#include "services/AppLog.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMessageLogContext>

#include <algorithm>
#include <format>
#include <memory>
#include <mutex>

namespace fdss::services {

namespace {

constexpr auto kBaseName = "FootballDSS";

struct LogState {
    std::mutex mutex;
    QString directory;
    int keepFiles = AppLog::kKeepFiles;
    qint64 maxBytes = AppLog::kMaxBytes;
    std::unique_ptr<QFile> file;
    QtMessageHandler previous = nullptr;
    bool installed = false;
    // A rotation that failed is not tried again until the next start, so the file only grows.
    bool rotationFailed = false;
};

LogState& state() {
    static LogState instance;
    return instance;
}

QString fileName(const QString& directory, int index) {
    const QString name =
        index == 0 ? QStringLiteral("%1.log").arg(QLatin1StringView(kBaseName))
                   : QStringLiteral("%1.%2.log").arg(QLatin1StringView(kBaseName)).arg(index);
    return QDir(directory).filePath(name);
}

// Opens FootballDSS.log for writing, with Truncate or Append.
bool openFile(LogState& log, QIODevice::OpenModeFlag mode) {
    auto file = std::make_unique<QFile>(fileName(log.directory, 0));
    if (!file->open(QIODevice::WriteOnly | mode | QIODevice::Text)) {
        return false;
    }
    log.file = std::move(file);
    return true;
}

// A line written straight to the file; the handler itself must not log.
void writeLine(LogState& log, const char* type, const QString& message) {
    const QString line = QStringLiteral("%1 [%2] default: %3\n")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                  QLatin1StringView(type), message);
    log.file->write(line.toUtf8());
    log.file->flush();
}

// Shifts FootballDSS.log to .1.log and so on, dropping the oldest, and opens a new file. When
// FootballDSS.log cannot be moved away (P19 review: in use, no rights), it is continued with
// Append, never truncated, and the file says why. Never logs: a message from here would come back
// into the handler.
bool startNewFile(LogState& log) {
    if (log.file) {
        log.file->close();
        log.file.reset();
    }
    QFile::remove(fileName(log.directory, log.keepFiles - 1));
    for (int index = log.keepFiles - 2; index >= 0; --index) {
        const QString from = fileName(log.directory, index);
        if (QFile::exists(from)) {
            QFile::rename(from, fileName(log.directory, index + 1));
        }
    }
    const QString current = fileName(log.directory, 0);
    if (log.keepFiles > 1 && QFile::exists(current)) {
        log.rotationFailed = true;
        if (!openFile(log, QIODevice::Append)) {
            return false;
        }
        writeLine(log, "warning",
                  QStringLiteral("AppLog: cannot rotate %1; it is continued instead").arg(current));
        return true;
    }
    return openFile(log, QIODevice::Truncate);
}

const char* typeName(QtMsgType type) {
    switch (type) {
    case QtDebugMsg:
        return "debug";
    case QtInfoMsg:
        return "info";
    case QtWarningMsg:
        return "warning";
    case QtCriticalMsg:
        return "critical";
    case QtFatalMsg:
        return "fatal";
    }
    return "message";
}

void handler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    LogState& log = state();
    QtMessageHandler previous = nullptr;
    {
        const std::lock_guard lock(log.mutex);
        previous = log.previous;
        if (log.file && !log.rotationFailed && log.file->size() >= log.maxBytes) {
            startNewFile(log);
        }
        if (log.file) {
            const QString line =
                QStringLiteral("%1 [%2] %3: %4\n")
                    .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                         QLatin1StringView(typeName(type)),
                         QLatin1StringView(context.category != nullptr ? context.category
                                                                       : "default"),
                         message);
            log.file->write(line.toUtf8());
            log.file->flush();
        }
    }
    if (previous != nullptr) {
        previous(type, context, message);
    }
}

} // namespace

core::Status AppLog::install(const QString& directory, int keepFiles, qint64 maxBytes,
                             Start start) {
    LogState& log = state();
    {
        const std::lock_guard lock(log.mutex);
        if (log.installed) {
            return std::unexpected(
                core::Error{core::ErrorCode::Internal, "the log is already installed", {}});
        }
        log.directory = directory;
        log.rotationFailed = false;
        log.keepFiles = std::max(keepFiles, 1);
        log.maxBytes = std::max<qint64>(maxBytes, 1);
        const bool opened =
            start == Start::Append ? openFile(log, QIODevice::Append) : startNewFile(log);
        if (!opened) {
            return std::unexpected(
                core::Error{core::ErrorCode::Io,
                            std::format("cannot open the log file in {}", directory.toStdString()),
                            {}});
        }
        log.installed = true;
    }
    // Outside the lock: the handler takes it.
    QtMessageHandler previous = qInstallMessageHandler(handler);
    const std::lock_guard lock(log.mutex);
    log.previous = previous;
    return {};
}

void AppLog::uninstall() {
    LogState& log = state();
    QtMessageHandler previous = nullptr;
    {
        const std::lock_guard lock(log.mutex);
        if (!log.installed) {
            return;
        }
        previous = log.previous;
    }
    qInstallMessageHandler(previous);
    const std::lock_guard lock(log.mutex);
    log.file.reset();
    log.previous = nullptr;
    log.installed = false;
}

QString AppLog::currentFile() {
    LogState& log = state();
    const std::lock_guard lock(log.mutex);
    return log.file ? log.file->fileName() : QString{};
}

} // namespace fdss::services
