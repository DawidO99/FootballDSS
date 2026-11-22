#include "viewmodels/AppLog.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMessageLogContext>

#include <algorithm>
#include <format>
#include <mutex>
#include <utility>

namespace fdss::viewmodels {

namespace {

constexpr auto kBaseName = "FootballDSS";

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

// The open log: the file, how it rotates, and the handler every line goes on to. Never logs
// itself: a message from here would come back into the handler.
class LogFile {
public:
    LogFile(QString directory, int keepFiles, qint64 maxBytes)
        : m_directory(std::move(directory)), m_keepFiles(std::max(keepFiles, 1)),
          m_maxBytes(std::max<qint64>(maxBytes, 1)) {}

    [[nodiscard]] bool open(AppLog::Start start) {
        return start == AppLog::Start::Append ? openFile(QIODevice::Append) : startNewFile();
    }

    void write(QtMsgType type, const QMessageLogContext& context, const QString& message) {
        if (m_file && !m_rotationFailed && m_file->size() >= m_maxBytes) {
            startNewFile();
        }
        writeLine(typeName(type), context.category != nullptr ? context.category : "default",
                  message);
    }

    [[nodiscard]] QString name() const { return m_file ? m_file->fileName() : QString{}; }
    [[nodiscard]] QtMessageHandler previous() const { return m_previous; }
    void setPrevious(QtMessageHandler previous) { m_previous = previous; }

private:
    [[nodiscard]] QString fileName(int index) const {
        const QString name =
            index == 0 ? QStringLiteral("%1.log").arg(QLatin1StringView(kBaseName))
                       : QStringLiteral("%1.%2.log").arg(QLatin1StringView(kBaseName)).arg(index);
        return QDir(m_directory).filePath(name);
    }

    bool openFile(QIODevice::OpenModeFlag mode) {
        auto file = std::make_unique<QFile>(fileName(0));
        if (!file->open(QIODevice::WriteOnly | mode | QIODevice::Text)) {
            return false;
        }
        m_file = std::move(file);
        return true;
    }

    // Shifts FootballDSS.log to .1.log and so on, dropping the oldest, and opens a new file. When
    // FootballDSS.log cannot be moved away (in use, no rights), it is continued with Append, never
    // truncated, the file says why, and no rotation is tried again until the next start.
    bool startNewFile() {
        m_file.reset();
        QFile::remove(fileName(m_keepFiles - 1));
        for (int index = m_keepFiles - 2; index >= 0; --index) {
            const QString from = fileName(index);
            if (QFile::exists(from)) {
                QFile::rename(from, fileName(index + 1));
            }
        }
        const QString current = fileName(0);
        if (m_keepFiles > 1 && QFile::exists(current)) {
            m_rotationFailed = true;
            if (!openFile(QIODevice::Append)) {
                return false;
            }
            writeLine(
                "warning", "default",
                QStringLiteral("AppLog: cannot rotate %1; it is continued instead").arg(current));
            return true;
        }
        return openFile(QIODevice::Truncate);
    }

    void writeLine(const char* type, const char* category, const QString& message) {
        if (!m_file) {
            return;
        }
        const QString line =
            QStringLiteral("%1 [%2] %3: %4\n")
                .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                     QLatin1StringView(type), QLatin1StringView(category), message);
        m_file->write(line.toUtf8());
        m_file->flush();
    }

    QString m_directory;
    int m_keepFiles;
    qint64 m_maxBytes;
    std::unique_ptr<QFile> m_file;
    bool m_rotationFailed = false;
    QtMessageHandler m_previous = nullptr;
};

// Forced by qInstallMessageHandler, which takes a plain function: the open file and its mutex.
// Only this file sees it (P19 decision 13).
struct LogState {
    std::mutex mutex;
    std::unique_ptr<LogFile> file;
};

LogState& state() {
    static LogState instance;
    return instance;
}

void handler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    LogState& log = state();
    QtMessageHandler previous = nullptr;
    {
        const std::lock_guard lock(log.mutex);
        if (log.file) {
            log.file->write(type, context, message);
            previous = log.file->previous();
        }
    }
    if (previous != nullptr) {
        previous(type, context, message);
    }
}

} // namespace

core::Result<std::unique_ptr<AppLog>> AppLog::install(const QString& directory, int keepFiles,
                                                      qint64 maxBytes, Start start) {
    LogState& log = state();
    const std::lock_guard lock(log.mutex);
    if (log.file) {
        return std::unexpected(
            core::Error{core::ErrorCode::Internal, "the log is already installed", {}});
    }
    auto file = std::make_unique<LogFile>(directory, keepFiles, maxBytes);
    if (!file->open(start)) {
        return std::unexpected(
            core::Error{core::ErrorCode::Io,
                        std::format("cannot open the log file in {}", directory.toStdString()),
                        {}});
    }
    // Installing calls no handler; a message of another thread waits for the lock.
    file->setPrevious(qInstallMessageHandler(handler));
    log.file = std::move(file);
    return std::make_unique<AppLog>(Key{});
}

AppLog::AppLog(Key /*key*/) {}

AppLog::~AppLog() {
    LogState& log = state();
    const std::lock_guard lock(log.mutex);
    if (log.file) {
        qInstallMessageHandler(log.file->previous());
        log.file.reset();
    }
}

QString AppLog::currentFile() const {
    LogState& log = state();
    const std::lock_guard lock(log.mutex);
    return log.file ? log.file->name() : QString{};
}

} // namespace fdss::viewmodels
