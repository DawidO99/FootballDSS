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

struct BackupFile {
    QString timestamp;
    int suffix = 1;
    QString name;
};

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

    const QDir target(directory);
    const QString stem = QStringLiteral("club-%1-%2")
                             .arg(now.toString(QStringLiteral("yyyyMMdd-HHmmss")), reasonText);
    QString path = target.filePath(stem + QStringLiteral(".sqlite"));
    for (int suffix = 2; QFileInfo::exists(path); ++suffix) {
        path = target.filePath(QStringLiteral("%1-%2.sqlite").arg(stem).arg(suffix));
    }

    QSqlQuery query(db);
    if (!query.prepare(QStringLiteral("VACUUM INTO ?"))) {
        return std::unexpected(sqlError(query.lastError(), "prepare backup"));
    }
    query.bindValue(0, path);
    if (!query.exec()) {
        // A failure half way (disk full) can leave a partial file that would count as a backup.
        QFile::remove(path);
        return std::unexpected(sqlError(query.lastError(), "write backup"));
    }
    return path;
}

core::Status pruneBackups(const QString& directory, int keep) {
    static const QRegularExpression kBackupName(
        QStringLiteral(R"(^club-(\d{8}-\d{6})-[a-z0-9_]+(?:-(\d+))?\.sqlite$)"));

    QDir target(directory);
    std::vector<BackupFile> backups;
    const QStringList names = target.entryList(QDir::Files);
    for (const QString& name : names) {
        const QRegularExpressionMatch match = kBackupName.match(name);
        if (match.hasMatch()) {
            const QString suffix = match.captured(2);
            backups.push_back({match.captured(1), suffix.isEmpty() ? 1 : suffix.toInt(), name});
        }
    }

    // Numeric suffix order: as plain file names, "-10" would sort before "-2" and "-2.sqlite"
    // before ".sqlite".
    std::ranges::sort(backups, [](const BackupFile& a, const BackupFile& b) {
        return std::tie(a.timestamp, a.suffix, a.name) > std::tie(b.timestamp, b.suffix, b.name);
    });

    core::Status result;
    for (const BackupFile& backup : backups | std::views::drop(std::max(keep, 0))) {
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
