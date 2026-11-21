#pragma once

#include "core/util/Error.hpp"
#include "viewmodels/RowsModel.hpp"

#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace fdss::services {
class MatchReportService;
} // namespace fdss::services

namespace fdss::viewmodels {

// The "Matches" tab of the player card (packet P14): the finished matches of a player, the most
// recent first. Roles: matchId, date (ISO), dateText, opponentName, venueText, resultKey (win,
// draw, loss), resultWord, scoreText ("2:1"), minutes, goals, assists, yellowCards, redCards,
// rating and rpe (int or nothing).
class PlayerMatchesModel : public RowsModel {
    Q_OBJECT
    QML_ELEMENT
    // 0: no player.
    Q_PROPERTY(qint64 playerId READ playerId WRITE setPlayerId NOTIFY playerIdChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    // Why the matches could not be read. Empty: they are fine.
    Q_PROPERTY(QString loadError READ loadError NOTIFY loadErrorChanged FINAL)

public:
    explicit PlayerMatchesModel(QObject* parent = nullptr);

    [[nodiscard]] qint64 playerId() const;
    void setPlayerId(qint64 playerId);
    [[nodiscard]] int count() const;
    [[nodiscard]] QString loadError() const;

    Q_INVOKABLE void reload();

signals:
    void playerIdChanged();
    void countChanged();
    void loadErrorChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    services::MatchReportService& m_service;
    qint64 m_playerId = 0;
    std::optional<core::Error> m_loadFailure;
};

} // namespace fdss::viewmodels
