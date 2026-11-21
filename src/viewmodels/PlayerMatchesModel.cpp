#include "viewmodels/PlayerMatchesModel.hpp"

#include "services/MatchReportService.hpp"
#include "viewmodels/CalendarTexts.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/IsoDates.hpp"
#include "viewmodels/ServiceRegistry.hpp"

#include <QCoreApplication>
#include <QEvent>

namespace fdss::viewmodels {

namespace {

QVariant optionalInt(const std::optional<int>& value) {
    return value ? QVariant(*value) : QVariant{};
}

} // namespace

PlayerMatchesModel::PlayerMatchesModel(QObject* parent)
    : RowsModel({"matchId", "date", "dateText", "opponentName", "venueText", "resultKey",
                 "resultWord", "scoreText", "minutes", "goals", "assists", "yellowCards",
                 "redCards", "rating", "rpe"},
                parent),
      m_service(ServiceRegistry::instance().matchReport()) {
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
}

qint64 PlayerMatchesModel::playerId() const { return m_playerId; }

void PlayerMatchesModel::setPlayerId(qint64 playerId) {
    if (playerId == m_playerId) {
        return;
    }
    m_playerId = playerId;
    emit playerIdChanged();
    reload();
}

int PlayerMatchesModel::count() const { return rowCount(); }

QString PlayerMatchesModel::loadError() const {
    return m_loadFailure ? ErrorPresenter::message(*m_loadFailure) : QString{};
}

void PlayerMatchesModel::reload() {
    QList<QVariantList> rows;
    m_loadFailure.reset();
    if (m_playerId > 0) {
        const auto matches = m_service.playerMatches(core::PlayerId{m_playerId});
        if (!matches) {
            qCritical("PlayerMatchesModel: %s", matches.error().detail.c_str());
            m_loadFailure = matches.error();
        } else {
            for (const services::PlayerMatchRow& match : *matches) {
                const int difference = match.goalsFor - match.goalsAgainst;
                const QString key = difference > 0   ? QStringLiteral("win")
                                    : difference < 0 ? QStringLiteral("loss")
                                                     : QStringLiteral("draw");
                const QString word = difference > 0   ? tr("Win")
                                     : difference < 0 ? tr("Loss")
                                                      : tr("Draw");
                rows.append(QVariantList{
                    QVariant::fromValue(match.match.value), IsoDates::toIso(match.date),
                    IsoDates::displayText(match.date), QString::fromStdString(match.opponentName),
                    CalendarTexts::venueName(match.venue), key, word,
                    QStringLiteral("%1:%2").arg(match.goalsFor).arg(match.goalsAgainst),
                    match.minutes, match.goals, match.assists, match.yellowCards, match.redCards,
                    optionalInt(match.rating), optionalInt(match.rpe)});
            }
        }
    }
    setRows(std::move(rows));
    emit countChanged();
    emit loadErrorChanged();
}

bool PlayerMatchesModel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == QCoreApplication::instance() && event->type() == QEvent::LanguageChange) {
        reload();
    }
    return RowsModel::eventFilter(watched, event);
}

} // namespace fdss::viewmodels
