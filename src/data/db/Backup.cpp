#include "data/db/Backup.hpp"

#include "data/db/DatabaseManager.hpp"

#include <QByteArrayView>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include <algorithm>
#include <format>
#include <ranges>
#include <tuple>
#include <vector>

namespace fdss::data {

namespace {

// The backup name pattern allows six digits: a longer suffix would not count as a backup.
constexpr int kMaxBackupSuffix = 999999;

struct BackupFile {
    QString timestamp;
    int suffix = 1; // a name without a suffix counts as 1
    QString name;
};

// Only names of this form count as backups; nothing else in the folder is ever touched.
std::vector<BackupFile> listBackups(const QDir& directory) {
    static const QRegularExpression kBackupName(
        QStringLiteral(R"(^club-(\d{8}-\d{6})-[a-z0-9_]+(?:-(\d{1,6}))?\.sqlite$)"));

    std::vector<BackupFile> backups;
    const QStringList names = directory.entryList(QDir::Files);
    for (const QString& name : names) {
        const QRegularExpressionMatch match = kBackupName.match(name);
        if (match.hasMatch()) {
            const QString suffix = match.captured(2);
            backups.push_back({match.captured(1), suffix.isEmpty() ? 1 : suffix.toInt(), name});
        }
    }
    return backups;
}

// Creates the empty target file. NewOnly claims the name atomically, so two calls never write to
// the same file. The suffix continues after the highest one of that second over all reasons, so the
// new backup sorts as the newest even after pruning removed the lower suffixes.
core::Result<QString> reserveBackupFile(const QDir& target, const QString& timestamp,
                                        const QString& reason) {
    int highest = 0;
    for (const BackupFile& backup : listBackups(target)) {
        if (backup.timestamp == timestamp) {
            highest = std::max(highest, backup.suffix);
        }
    }

    const QString stem = QStringLiteral("club-%1-%2").arg(timestamp, reason);
    for (int suffix = highest + 1; suffix <= kMaxBackupSuffix; ++suffix) {
        const QString path =
            target.filePath(suffix == 1 ? stem + QStringLiteral(".sqlite")
                                        : QStringLiteral("%1-%2.sqlite").arg(stem).arg(suffix));
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            return path;
        }
        // Another call took this name, so try the next one. Anything else is a real failure.
        if (!QFileInfo::exists(path)) {
            return std::unexpected(
                core::Error{core::ErrorCode::Io,
                            std::format("cannot create backup file {}: {}", path.toStdString(),
                                        file.errorString().toStdString()),
                            {}});
        }
    }
    return std::unexpected(
        core::Error{core::ErrorCode::Internal,
                    std::format("no backup suffix left for {}", stem.toStdString()),
                    {}});
}

} // namespace

core::Result<QString> createBackup(const QSqlDatabase& db, const QString& directory,
                                   std::string_view reason, const QDateTime& now) {
    static const QRegularExpression kReason(QStringLiteral("^[a-z0-9_]+$"));

    const QString reasonText = QString::fromUtf8(QByteArrayView(reason));
    if (!kReason.match(reasonText).hasMatch()) {
        return std::unexpected(core::Error{core::ErrorCode::InvalidInput,
                                           std::format("invalid backup reason '{}'", reason),
                                           "reason"});
    }
    if (!QDir().mkpath(directory)) {
        return std::unexpected(
            core::Error{core::ErrorCode::Io,
                        std::format("cannot create backup folder {}", directory.toStdString()),
                        {}});
    }

    QSqlQuery query(db);
    if (!query.prepare(QStringLiteral("VACUUM INTO ?"))) {
        return std::unexpected(describeSqlError(query.lastError(), "prepare backup"));
    }

    const core::Result<QString> path = reserveBackupFile(
        QDir(directory), now.toString(QStringLiteral("yyyyMMdd-HHmmss")), reasonText);
    if (!path) {
        return path;
    }
    query.bindValue(0, *path);
    if (!query.exec()) {
        // The file reserved above, and only that one: this call created it. A failure half way
        // (disk full) can also leave a partial copy that would count as a backup.
        QFile::remove(*path);
        return std::unexpected(describeSqlError(query.lastError(), "write backup"));
    }
    return path;
}

core::Status pruneBackups(const QString& directory, const QString& protectedPath, int keep) {
    QDir target(directory);
    std::vector<BackupFile> backups = listBackups(target);

    // Newest first in numeric suffix order: as plain file names, "-10" would sort before "-2" and
    // "-2.sqlite" before ".sqlite". The protected backup goes ahead of all of them, so it takes one
    // of the kept places even when its name is the oldest (the clock was moved back).
    const QString protectedName = QFileInfo(protectedPath).fileName();
    std::ranges::sort(backups, [&protectedName](const BackupFile& a, const BackupFile& b) {
        const bool aProtected = a.name == protectedName;
        const bool bProtected = b.name == protectedName;
        if (aProtected != bProtected) {
            return aProtected;
        }
        return std::tie(a.timestamp, a.suffix, a.name) > std::tie(b.timestamp, b.suffix, b.name);
    });

    core::Status result;
    for (const BackupFile& backup : backups | std::views::drop(std::max(keep, 1))) {
        // Keep going: one locked file must not stop the others from being removed.
        if (!target.remove(backup.name) && result) {
            result = std::unexpected(
                core::Error{core::ErrorCode::Io,
                            std::format("cannot delete old backup {}", backup.name.toStdString()),
                            {}});
        }
    }
    return result;
}

} // namespace fdss::data
