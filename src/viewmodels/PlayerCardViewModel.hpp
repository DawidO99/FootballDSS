#pragma once

#include "services/SquadService.hpp"

#include <QObject>
#include <QString>
#include <QVariant>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace fdss::services {
class Clock;
class Session;
} // namespace fdss::services

namespace fdss::viewmodels {

// The header and the data of the player card. Without a player or a session everything is empty.
class PlayerCardViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // 0: no player.
    Q_PROPERTY(qint64 playerId READ playerId WRITE setPlayerId NOTIFY playerIdChanged FINAL)
    // An int or null.
    Q_PROPERTY(QVariant number READ number NOTIFY playerChanged FINAL)
    Q_PROPERTY(QString name READ name NOTIFY playerChanged FINAL)
    Q_PROPERTY(QString positionCode READ positionCode NOTIFY playerChanged FINAL)
    Q_PROPERTY(QString positionName READ positionName NOTIFY playerChanged FINAL)
    // Codes of the secondary positions, comma separated.
    Q_PROPERTY(QString secondaryPositions READ secondaryPositions NOTIFY playerChanged FINAL)
    // Ints or null.
    Q_PROPERTY(QVariant age READ age NOTIFY playerChanged FINAL)
    Q_PROPERTY(QVariant birthYear READ birthYear NOTIFY playerChanged FINAL)
    Q_PROPERTY(QString footName READ footName NOTIFY playerChanged FINAL)
    Q_PROPERTY(QString notes READ notes NOTIFY playerChanged FINAL)
    Q_PROPERTY(QString statusKey READ statusKey NOTIFY playerChanged FINAL)
    Q_PROPERTY(QString statusText READ statusText NOTIFY playerChanged FINAL)
    Q_PROPERTY(bool archived READ archived NOTIFY playerChanged FINAL)
    // Why the player could not be read. Empty: the data is fine.
    Q_PROPERTY(QString loadError READ loadError NOTIFY loadErrorChanged FINAL)

public:
    explicit PlayerCardViewModel(QObject* parent = nullptr);

    [[nodiscard]] qint64 playerId() const;
    void setPlayerId(qint64 playerId);
    [[nodiscard]] QVariant number() const;
    [[nodiscard]] QString name() const;
    [[nodiscard]] QString positionCode() const;
    [[nodiscard]] QString positionName() const;
    [[nodiscard]] QString secondaryPositions() const;
    [[nodiscard]] QVariant age() const;
    [[nodiscard]] QVariant birthYear() const;
    [[nodiscard]] QString footName() const;
    [[nodiscard]] QString notes() const;
    [[nodiscard]] QString statusKey() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] bool archived() const;
    [[nodiscard]] QString loadError() const;

    Q_INVOKABLE void reload();

signals:
    void playerIdChanged();
    void playerChanged();
    void loadErrorChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    services::SquadService& m_squad;
    services::Session& m_session;
    const services::Clock& m_clock;

    qint64 m_playerId = 0;
    std::optional<services::SquadPlayer> m_player;
    std::optional<core::Error> m_loadFailure;
};

} // namespace fdss::viewmodels
