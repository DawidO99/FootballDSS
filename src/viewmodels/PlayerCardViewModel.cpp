#include "viewmodels/PlayerCardViewModel.hpp"

#include "services/Clock.hpp"
#include "services/Session.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/ServiceRegistry.hpp"
#include "viewmodels/SquadTexts.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QStringList>

#include <utility>

namespace fdss::viewmodels {

PlayerCardViewModel::PlayerCardViewModel(QObject* parent)
    : QObject(parent), m_squad(ServiceRegistry::instance().squad()),
      m_session(ServiceRegistry::instance().session()),
      m_clock(ServiceRegistry::instance().clock()) {
    // Installing a translator sends QEvent::LanguageChange to the application object, which only
    // an application-wide filter sees; eventFilter() ignores the copies Qt sends on to the items.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
}

qint64 PlayerCardViewModel::playerId() const { return m_playerId; }

void PlayerCardViewModel::setPlayerId(qint64 playerId) {
    if (playerId == m_playerId) {
        return;
    }
    m_playerId = playerId;
    emit playerIdChanged();
    reload();
}

QVariant PlayerCardViewModel::number() const {
    return m_player && m_player->player.shirtNumber ? QVariant(*m_player->player.shirtNumber)
                                                    : QVariant{};
}

QString PlayerCardViewModel::name() const {
    return m_player ? SquadTexts::displayName(m_player->player) : QString{};
}

QString PlayerCardViewModel::positionCode() const {
    // Empty without a position: the card shows "Position not given" alone (P19).
    return m_player && m_player->player.primaryPosition
               ? SquadTexts::positionCode(*m_player->player.primaryPosition)
               : QString{};
}

QString PlayerCardViewModel::positionName() const {
    return m_player ? SquadTexts::positionName(m_player->player.primaryPosition) : QString{};
}

QString PlayerCardViewModel::secondaryPositions() const {
    if (!m_player) {
        return {};
    }
    QStringList codes;
    for (const core::Position position : m_player->player.secondaryPositions) {
        codes.append(SquadTexts::positionCode(position));
    }
    return codes.join(QStringLiteral(", "));
}

QVariant PlayerCardViewModel::age() const {
    return m_player ? SquadTexts::age(m_player->player.birthYear,
                                      static_cast<int>(m_clock.today().year()))
                    : QVariant{};
}

QVariant PlayerCardViewModel::birthYear() const {
    return m_player && m_player->player.birthYear ? QVariant(*m_player->player.birthYear)
                                                  : QVariant{};
}

QString PlayerCardViewModel::footName() const {
    return m_player ? SquadTexts::footName(m_player->player.foot) : QString{};
}

QString PlayerCardViewModel::notes() const {
    return m_player ? QString::fromStdString(m_player->player.notes.value_or(std::string{}))
                    : QString{};
}

QString PlayerCardViewModel::statusKey() const {
    return m_player ? SquadTexts::statusKey(m_player->status, archived()) : QString{};
}

QString PlayerCardViewModel::statusText() const {
    return m_player ? SquadTexts::statusName(m_player->status, archived()) : QString{};
}

bool PlayerCardViewModel::archived() const {
    return m_player && m_player->player.archivedAt.has_value();
}

QString PlayerCardViewModel::loadError() const {
    return m_loadFailure ? ErrorPresenter::message(*m_loadFailure) : QString{};
}

void PlayerCardViewModel::reload() {
    m_player.reset();
    m_loadFailure.reset();
    if (m_playerId != 0 && m_session.user()) {
        auto player = m_squad.player(core::PlayerId{m_playerId});
        if (player) {
            m_player = std::move(*player);
        } else {
            qCritical("PlayerCardViewModel: %s", player.error().detail.c_str());
            m_loadFailure = player.error();
        }
    }
    emit playerChanged();
    emit loadErrorChanged();
}

bool PlayerCardViewModel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == QCoreApplication::instance() && event->type() == QEvent::LanguageChange) {
        emit playerChanged();
        emit loadErrorChanged();
    }
    return QObject::eventFilter(watched, event);
}

} // namespace fdss::viewmodels
