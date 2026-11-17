#include "viewmodels/MatchDayViewModel.hpp"

#include "core/domain/Permissions.hpp"
#include "services/Clock.hpp"
#include "services/LineupService.hpp"
#include "services/Session.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/LineupTexts.hpp"
#include "viewmodels/MatchDayTexts.hpp"
#include "viewmodels/ServiceRegistry.hpp"
#include "viewmodels/SquadTexts.hpp"

#include <QCoreApplication>
#include <QEvent>

#include <algorithm>
#include <ranges>

namespace fdss::viewmodels {

namespace {

constexpr int kTickMs = 500;

QVariant shirtNumberValue(const std::optional<int>& number) {
    return number ? QVariant(*number) : QVariant{};
}

} // namespace

MatchDayViewModel::MatchDayViewModel(QObject* parent)
    : QObject(parent), m_service(ServiceRegistry::instance().matchDay()),
      m_lineupService(ServiceRegistry::instance().lineup()),
      m_session(ServiceRegistry::instance().session()),
      m_clock(ServiceRegistry::instance().clock()), m_pitch(new MatchDayPlayersModel(this)),
      m_bench(new MatchDayPlayersModel(this)), m_timeline(new MatchTimelineModel(this)) {
    m_timer.setInterval(kTickMs);
    connect(&m_timer, &QTimer::timeout, this, &MatchDayViewModel::tick);
    // Once per change of the language, see LoadViewModel.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
}

qint64 MatchDayViewModel::teamId() const { return m_teamId; }

void MatchDayViewModel::setTeamId(qint64 teamId) {
    if (teamId == m_teamId) {
        return;
    }
    m_teamId = teamId;
    emit teamIdChanged();
}

int MatchDayViewModel::yellowCardThreshold() const { return m_yellowCardThreshold; }

void MatchDayViewModel::setYellowCardThreshold(int threshold) {
    if (threshold == m_yellowCardThreshold) {
        return;
    }
    m_yellowCardThreshold = threshold;
    emit yellowCardThresholdChanged();
    if (m_state && m_state->match.match.status == core::MatchStatus::Planned) {
        reload();
    }
}

qint64 MatchDayViewModel::matchId() const { return m_state ? m_state->match.match.id.value : 0; }

bool MatchDayViewModel::hasMatch() const { return m_state.has_value(); }

QString MatchDayViewModel::opponentName() const {
    return m_state ? QString::fromStdString(m_state->match.opponentName) : QString{};
}

QString MatchDayViewModel::matchDetailsText() const {
    return m_state ? LineupTexts::matchDetails(m_state->match.match) : QString{};
}

QString MatchDayViewModel::statusKey() const {
    return m_state ? QString::fromUtf8(core::toDbText(m_state->match.match.status)) : QString{};
}

QString MatchDayViewModel::statusText() const {
    return m_state ? MatchDayTexts::statusName(m_state->match.match.status) : QString{};
}

bool MatchDayViewModel::guardLeaving() const {
    return m_state && m_state->match.match.status == core::MatchStatus::Live && !readOnly();
}

bool MatchDayViewModel::readOnly() const {
    return !m_session.isAllowed(core::Permission::RunMatchDay);
}

QString MatchDayViewModel::readOnlyText() const {
    if (!readOnly()) {
        return {};
    }
    return tr("Only the coach or the assistant can run Match Day. You see the match read-only.");
}

QString MatchDayViewModel::startBlock() const {
    if (!m_startCheck) {
        return {};
    }
    switch (m_startCheck->block) {
    case services::MatchDayStartCheck::Block::None:
        return {};
    case services::MatchDayStartCheck::Block::NotPlanned:
        return QStringLiteral("notPlanned");
    case services::MatchDayStartCheck::Block::AnotherLive:
        return QStringLiteral("anotherLive");
    case services::MatchDayStartCheck::Block::NoLineup:
        return QStringLiteral("noLineup");
    case services::MatchDayStartCheck::Block::LineupErrors:
        return QStringLiteral("lineupErrors");
    }
    return {};
}

QString MatchDayViewModel::startBlockText() const {
    if (!m_startCheck) {
        return {};
    }
    switch (m_startCheck->block) {
    case services::MatchDayStartCheck::Block::None:
        return {};
    case services::MatchDayStartCheck::Block::NotPlanned:
        return tr("This match is not planned, so Match Day cannot start it.");
    case services::MatchDayStartCheck::Block::AnotherLive:
        return tr("Another match of this team is live. Finish it in Match Day first.");
    case services::MatchDayStartCheck::Block::NoLineup:
        return tr("This match has no saved lineup. Set up and save the lineup in Pre-match.");
    case services::MatchDayStartCheck::Block::LineupErrors:
        return tr("The lineup has errors to fix in Pre-match: %1.").arg(m_startCheck->lineupErrors);
    }
    return {};
}

bool MatchDayViewModel::canStart() const {
    return m_state && m_state->match.match.status == core::MatchStatus::Planned && m_startCheck &&
           m_startCheck->block == services::MatchDayStartCheck::Block::None && !readOnly();
}

int MatchDayViewModel::defaultHalfLength() const {
    return m_startCheck ? m_startCheck->defaultHalfLength
                        : (m_state ? core::defaultHalfLength(m_state->category) : 45);
}

int MatchDayViewModel::minHalfLength() { return core::kMinHalfLength; }

int MatchDayViewModel::maxHalfLength() { return core::kMaxHalfLength; }

std::optional<core::ClockReading> MatchDayViewModel::reading() const {
    if (!m_state || !m_state->clock) {
        return std::nullopt;
    }
    return core::readClock(*m_state->clock, m_clock.now());
}

core::MatchPhase MatchDayViewModel::currentPhase() const {
    return m_state && m_state->clock ? core::phaseOf(*m_state->clock)
                                     : core::MatchPhase::NotStarted;
}

QString MatchDayViewModel::phase() const {
    return m_state && m_state->clock ? MatchDayTexts::phaseKey(currentPhase()) : QString{};
}

QString MatchDayViewModel::phaseText() const {
    return m_state && m_state->clock ? MatchDayTexts::phaseName(currentPhase()) : QString{};
}

QString MatchDayViewModel::clockText() const {
    const auto now = reading();
    return now ? MatchDayTexts::clockText(*now, m_state->clock->halfLength)
               : QStringLiteral("00:00");
}

QString MatchDayViewModel::clockActionText() const {
    if (!m_state || !m_state->clock) {
        return {};
    }
    return MatchDayTexts::clockActionText(core::nextStep(*m_state->clock));
}

bool MatchDayViewModel::canAdvanceClock() const {
    return m_state && m_state->clock && m_state->match.match.status == core::MatchStatus::Live &&
           !readOnly();
}

bool MatchDayViewModel::canCancelStart() const {
    return canAdvanceClock() && currentPhase() == core::MatchPhase::NotStarted;
}

bool MatchDayViewModel::canRecordEvents() const {
    const core::MatchPhase phase = currentPhase();
    return canAdvanceClock() &&
           (phase == core::MatchPhase::FirstHalf || phase == core::MatchPhase::SecondHalf);
}

bool MatchDayViewModel::canSubstitute() const {
    return canRecordEvents() || (canAdvanceClock() && currentPhase() == core::MatchPhase::HalfTime);
}

int MatchDayViewModel::goalsFor() const { return m_state ? m_state->situation.goalsFor : 0; }

int MatchDayViewModel::goalsAgainst() const {
    return m_state ? m_state->situation.goalsAgainst : 0;
}

bool MatchDayViewModel::canUndo() const {
    return m_state && m_state->lastAction.has_value() && !readOnly();
}

QString MatchDayViewModel::undoText() const {
    return m_state && m_state->lastAction ? actionText(*m_state->lastAction) : QString{};
}

MatchDayPlayersModel* MatchDayViewModel::pitch() const { return m_pitch; }

MatchDayPlayersModel* MatchDayViewModel::bench() const { return m_bench; }

MatchTimelineModel* MatchDayViewModel::timeline() const { return m_timeline; }

QString MatchDayViewModel::pendingAction() const {
    switch (m_pending) {
    case Pending::None:
        return {};
    case Pending::Scorer:
        return QStringLiteral("scorer");
    case Pending::Assist:
        return QStringLiteral("assist");
    case Pending::Yellow:
        return QStringLiteral("yellow");
    case Pending::Red:
        return QStringLiteral("red");
    case Pending::SubOff:
        return QStringLiteral("subOff");
    case Pending::SubOn:
        return QStringLiteral("subOn");
    }
    return {};
}

QString MatchDayViewModel::pendingPrompt() const {
    switch (m_pending) {
    case Pending::None:
        return {};
    case Pending::Scorer:
        return tr("Goal: who scored? Tap a player on the pitch.");
    case Pending::Assist:
        return tr("Goal by %1: who gave the assist? Tap a player, or choose No assist.")
            .arg(playerName(m_pendingFirst));
    case Pending::Yellow:
        return tr("Yellow card: tap the player on the pitch.");
    case Pending::Red:
        return tr("Red card: tap the player on the pitch.");
    case Pending::SubOff:
        return tr("Substitution: who goes off? Tap a player on the pitch.");
    case Pending::SubOn:
        return tr("%1 goes off: who comes on? Tap a player on the bench.")
            .arg(playerName(m_pendingFirst));
    }
    return {};
}

QString MatchDayViewModel::loadError() const {
    return m_loadFailure ? ErrorPresenter::message(*m_loadFailure) : QString{};
}

void MatchDayViewModel::openMatch(qint64 matchId) {
    if (matchId <= 0) {
        openDefaultMatch();
        return;
    }
    read(core::MatchId{matchId});
}

void MatchDayViewModel::openDefaultMatch() {
    if (m_teamId == 0 || !m_session.user()) {
        clearMatch(std::nullopt);
        return;
    }
    const core::TeamId team{m_teamId};
    auto live = m_service.liveMatch(team);
    if (!live) {
        clearMatch(live.error());
        return;
    }
    if (*live) {
        read(**live);
        return;
    }
    auto next = m_lineupService.nextPlannedMatch(team);
    if (!next) {
        clearMatch(next.error());
        return;
    }
    if (*next) {
        read(**next);
    } else {
        clearMatch(std::nullopt);
    }
}

void MatchDayViewModel::reload() {
    if (m_state) {
        read(m_state->match.match.id);
    } else {
        openDefaultMatch();
    }
}

void MatchDayViewModel::read(core::MatchId match) {
    auto state = m_service.state(match);
    if (!state) {
        qWarning("MatchDayViewModel: %s", state.error().detail.c_str());
        clearMatch(state.error());
        return;
    }
    std::optional<services::MatchDayStartCheck> check;
    if (state->match.match.status == core::MatchStatus::Planned) {
        auto checked = m_service.startCheck(match, m_yellowCardThreshold);
        if (!checked) {
            qWarning("MatchDayViewModel: %s", checked.error().detail.c_str());
            clearMatch(checked.error());
            return;
        }
        check = *checked;
    }
    const bool otherMatch = !m_state || m_state->match.match.id != match;
    m_state = std::move(*state);
    m_startCheck = check;
    const bool hadFailure = m_loadFailure.has_value();
    m_loadFailure.reset();
    if (otherMatch || m_state->match.match.status != core::MatchStatus::Live) {
        m_pending = Pending::None;
        m_pendingFirst.reset();
    }
    rebuildRows();
    updateTimer();
    emit matchChanged();
    emit clockChanged();
    emit eventsChanged();
    emit pendingChanged();
    emit textsChanged();
    if (hadFailure) {
        emit loadErrorChanged();
    }
}

void MatchDayViewModel::clearMatch(std::optional<core::Error> failure) {
    m_state.reset();
    m_startCheck.reset();
    m_loadFailure = std::move(failure);
    m_pending = Pending::None;
    m_pendingFirst.reset();
    m_pitch->setRows({});
    m_bench->setRows({});
    m_timeline->setRows({});
    updateTimer();
    emit matchChanged();
    emit clockChanged();
    emit eventsChanged();
    emit pendingChanged();
    emit textsChanged();
    emit loadErrorChanged();
}

QString MatchDayViewModel::playerName(std::optional<core::PlayerId> player) const {
    if (!m_state || !player) {
        return {};
    }
    const auto found = std::ranges::find(m_state->players, *player, &core::Player::id);
    return found == m_state->players.end() ? QString{} : SquadTexts::displayName(*found);
}

QString MatchDayViewModel::actionText(const services::MatchDayAction& action) const {
    if (action.kind == services::MatchDayAction::Kind::ClockStep) {
        return action.step ? MatchDayTexts::clockStepName(*action.step) : QString{};
    }
    if (!action.event) {
        return {};
    }
    const core::MatchEvent& event = *action.event;
    const bool secondYellow = action.kind == services::MatchDayAction::Kind::SecondYellow;
    const core::MatchEventType type = secondYellow ? core::MatchEventType::Red : event.type;
    QString who;
    if (event.type == core::MatchEventType::Sub) {
        who = tr("%1 for %2").arg(playerName(event.playerId), playerName(event.relatedPlayerId));
    } else if (event.type == core::MatchEventType::OpponentGoal) {
        who = opponentName();
    } else {
        who = playerName(event.playerId);
    }
    return tr("%1 · %2 %3")
        .arg(MatchDayTexts::eventName(type, secondYellow), who,
             MatchDayTexts::minuteText(event.minute, event.addedTime));
}

void MatchDayViewModel::rebuildRows() {
    if (!m_state) {
        return;
    }
    const auto now = reading();
    const int currentMinute = now ? now->eventTime.minute : 0;
    m_rowsMinute = currentMinute;
    const bool finished = m_state->match.match.status == core::MatchStatus::Finished;
    const int finalMinute = m_state->clock ? 2 * m_state->clock->halfLength : currentMinute;

    QList<QVariantList> pitchRows;
    QList<QVariantList> benchRows;
    QList<QVariantList> offRows;
    for (const core::PlayerInMatch& player : m_state->situation.players) {
        const auto found = std::ranges::find(m_state->players, player.player, &core::Player::id);
        const core::Player* info = found == m_state->players.end() ? nullptr : &*found;
        QString minutesText;
        if (player.minuteOn && m_state->clock) {
            const int end = player.minuteOff.value_or(finished ? finalMinute : currentMinute);
            minutesText = QStringLiteral("%1'").arg(std::max(0, end - *player.minuteOn));
        }
        bool selectable = false;
        switch (m_pending) {
        case Pending::None:
            break;
        case Pending::Scorer:
        case Pending::Yellow:
        case Pending::Red:
        case Pending::SubOff:
            selectable = player.state == core::PlayerMatchState::OnPitch;
            break;
        case Pending::Assist:
            selectable =
                player.state == core::PlayerMatchState::OnPitch && player.player != m_pendingFirst;
            break;
        case Pending::SubOn:
            selectable = player.state == core::PlayerMatchState::Bench;
            break;
        }
        QVariantList row{QVariant::fromValue(player.player.value),
                         info ? shirtNumberValue(info->shirtNumber) : QVariant{},
                         info ? SquadTexts::displayName(*info) : QString{},
                         info ? QString::fromStdString(info->lastName) : QString{},
                         player.position
                             ? SquadTexts::positionCode(*player.position)
                             : (info ? SquadTexts::positionCode(info->primaryPosition) : QString{}),
                         MatchDayTexts::playerStateKey(player.state),
                         MatchDayTexts::playerStateName(player.state),
                         player.yellowCards,
                         player.goals,
                         minutesText,
                         selectable};
        switch (player.state) {
        case core::PlayerMatchState::OnPitch:
            pitchRows.append(std::move(row));
            break;
        case core::PlayerMatchState::Bench:
            benchRows.append(std::move(row));
            break;
        case core::PlayerMatchState::SubstitutedOff:
        case core::PlayerMatchState::SentOff:
            offRows.append(std::move(row));
            break;
        }
    }
    benchRows.append(offRows);
    m_pitch->setRows(std::move(pitchRows));
    m_bench->setRows(std::move(benchRows));

    QList<QVariantList> events;
    const std::vector<core::MatchEvent>& list = m_state->events;
    const std::vector<core::MatchEventDraft> drafts(list.begin(), list.end());
    // Counted in replay order from the yellow cards before each red (P13 review).
    const std::vector<bool> secondYellows = core::secondYellowReds(drafts);
    for (std::size_t index = 0; index < list.size(); ++index) {
        const core::MatchEvent& event = list[index];
        const bool secondYellow = secondYellows[index];
        QString playerText;
        QString relatedText;
        switch (event.type) {
        case core::MatchEventType::Goal:
            playerText = playerName(event.playerId);
            if (event.relatedPlayerId) {
                relatedText = tr("Assist: %1").arg(playerName(event.relatedPlayerId));
            }
            break;
        case core::MatchEventType::Sub:
            playerText = tr("On: %1").arg(playerName(event.playerId));
            relatedText = tr("Off: %1").arg(playerName(event.relatedPlayerId));
            break;
        case core::MatchEventType::OpponentGoal:
            playerText = opponentName();
            break;
        case core::MatchEventType::Assist:
        case core::MatchEventType::Yellow:
        case core::MatchEventType::Red:
            playerText = playerName(event.playerId);
            break;
        }
        events.prepend(QVariantList{
            QVariant::fromValue(event.id.value),
            MatchDayTexts::minuteText(event.minute, event.addedTime),
            MatchDayTexts::eventKey(event.type, secondYellow), MatchDayTexts::eventIcon(event.type),
            MatchDayTexts::eventName(event.type, secondYellow), playerText, relatedText});
    }
    m_timeline->setRows(std::move(events));
}

void MatchDayViewModel::updateTimer() {
    const core::MatchPhase phase = currentPhase();
    const bool running =
        m_state && m_state->clock &&
        (phase == core::MatchPhase::FirstHalf || phase == core::MatchPhase::SecondHalf);
    if (running && !m_timer.isActive()) {
        m_timer.start();
    } else if (!running) {
        m_timer.stop();
    }
}

void MatchDayViewModel::tick() {
    emit clockChanged();
    const auto now = reading();
    if (now && now->eventTime.minute != m_rowsMinute) {
        rebuildRows();
    }
}

void MatchDayViewModel::report(const core::Error& error) {
    qWarning("MatchDayViewModel: %s", error.detail.c_str());
    emit errorOccurred(ErrorPresenter::message(error));
}

bool MatchDayViewModel::checkRunnable() {
    if (!m_state) {
        return false;
    }
    if (readOnly()) {
        emit errorOccurred(readOnlyText());
        return false;
    }
    return true;
}

bool MatchDayViewModel::finish(const core::Status& status) {
    if (!status) {
        report(status.error());
        // The match may have changed meanwhile (e.g. finished elsewhere): show what is stored.
        reload();
        return false;
    }
    setPending(Pending::None);
    reload();
    return true;
}

bool MatchDayViewModel::start(int halfLength) {
    if (!checkRunnable()) {
        return false;
    }
    return finish(m_service.start(m_state->match.match.id, halfLength, m_yellowCardThreshold));
}

bool MatchDayViewModel::cancelStart() {
    if (!checkRunnable()) {
        return false;
    }
    return finish(m_service.cancelStart(m_state->match.match.id));
}

bool MatchDayViewModel::advanceClock() {
    if (!checkRunnable()) {
        return false;
    }
    return finish(m_service.advanceClock(m_state->match.match.id));
}

void MatchDayViewModel::setPending(Pending pending, std::optional<core::PlayerId> first) {
    if (pending == m_pending && first == m_pendingFirst) {
        return;
    }
    m_pending = pending;
    m_pendingFirst = first;
    rebuildRows();
    emit pendingChanged();
}

void MatchDayViewModel::beginAction(const QString& action) {
    if (!m_state || readOnly()) {
        setPending(Pending::None);
        return;
    }
    if (action == QStringLiteral("goal") && canRecordEvents()) {
        setPending(Pending::Scorer);
    } else if (action == QStringLiteral("yellow") && canRecordEvents()) {
        setPending(Pending::Yellow);
    } else if (action == QStringLiteral("red") && canRecordEvents()) {
        setPending(Pending::Red);
    } else if (action == QStringLiteral("sub") && canSubstitute()) {
        setPending(Pending::SubOff);
    } else {
        setPending(Pending::None);
    }
}

bool MatchDayViewModel::choosePlayer(qint64 playerId) {
    if (!checkRunnable() || m_pending == Pending::None) {
        return false;
    }
    const core::MatchId match = m_state->match.match.id;
    const core::PlayerId player{playerId};
    switch (m_pending) {
    case Pending::None:
        return false;
    case Pending::Scorer:
        setPending(Pending::Assist, player);
        return true;
    case Pending::Assist:
        return finish(m_service.recordGoal(match, *m_pendingFirst, player));
    case Pending::Yellow:
        return finish(m_service.recordYellow(match, player));
    case Pending::Red:
        return finish(m_service.recordRed(match, player));
    case Pending::SubOff:
        setPending(Pending::SubOn, player);
        return true;
    case Pending::SubOn:
        return finish(m_service.recordSubstitution(match, player, *m_pendingFirst));
    }
    return false;
}

bool MatchDayViewModel::skipAssist() {
    if (!checkRunnable() || m_pending != Pending::Assist || !m_pendingFirst) {
        return false;
    }
    return finish(m_service.recordGoal(m_state->match.match.id, *m_pendingFirst, std::nullopt));
}

void MatchDayViewModel::cancelAction() { setPending(Pending::None); }

bool MatchDayViewModel::recordOpponentGoal() {
    if (!checkRunnable()) {
        return false;
    }
    setPending(Pending::None);
    return finish(m_service.recordOpponentGoal(m_state->match.match.id));
}

QString MatchDayViewModel::undo() {
    if (!checkRunnable()) {
        return {};
    }
    setPending(Pending::None);
    // Names come from the state before the undo.
    const auto undone = m_service.undoLast(m_state->match.match.id);
    if (!undone) {
        if (undone.error().field == "undo_end_another_live") {
            // Names the match to finish or to take back to planned first (P13 review).
            qWarning("MatchDayViewModel: %s", undone.error().detail.c_str());
            emit errorOccurred(anotherLiveMatchText());
        } else {
            report(undone.error());
        }
        reload();
        return {};
    }
    const QString text = tr("Undone: %1").arg(actionText(*undone));
    reload();
    return text;
}

QString MatchDayViewModel::anotherLiveMatchText() const {
    if (m_state) {
        if (const auto other = m_service.liveMatch(m_state->match.match.teamId); other && *other) {
            if (const auto state = m_service.state(**other)) {
                return tr("Another match of this team is live. First finish it, or undo its start, "
                          "in the match against %1.")
                    .arg(QString::fromStdString(state->match.opponentName));
            }
        }
    }
    return ErrorPresenter::message(
        core::Error{core::ErrorCode::Conflict, {}, "undo_end_another_live"});
}

bool MatchDayViewModel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == QCoreApplication::instance() && event->type() == QEvent::LanguageChange) {
        rebuildRows();
        emit textsChanged();
        emit clockChanged();
        emit eventsChanged();
        emit pendingChanged();
        emit loadErrorChanged();
    }
    return QObject::eventFilter(watched, event);
}

} // namespace fdss::viewmodels
