#include "viewmodels/MatchDayViewModel.hpp"

#include "core/algorithms/Formations.hpp"
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
#include <QVariantMap>

#include <algorithm>
#include <ranges>
#include <tuple>

namespace fdss::viewmodels {

namespace {

constexpr int kTickMs = 500;
// A note in the undo text: its beginning only.
constexpr int kNotePreview = 40;

QVariant shirtNumberValue(const std::optional<int>& number) {
    return number ? QVariant(*number) : QVariant{};
}

QString formatKey(core::MatchFormat format) {
    return QStringLiteral("%1x%2").arg(format.periods).arg(format.periodLength);
}

// The order of the match for the timeline: by minute, the added time of a part before the start
// of the next one.
std::tuple<int, int, int> matchOrder(int minute, std::optional<int> addedTime) {
    return {minute, addedTime ? 0 : 1, addedTime.value_or(0)};
}

const auto kCustomShape = QStringLiteral("custom");

QVariantMap choice(const QString& value, const QString& text) {
    return QVariantMap{{QStringLiteral("value"), value}, {QStringLiteral("text"), text}};
}

} // namespace

MatchDayViewModel::MatchDayViewModel(QObject* parent)
    : QObject(parent), m_service(ServiceRegistry::instance().matchDay()),
      m_lineupService(ServiceRegistry::instance().lineup()),
      m_session(ServiceRegistry::instance().session()),
      m_clock(ServiceRegistry::instance().clock()), m_pitch(new MatchDayPlayersModel(this)),
      m_bench(new MatchDayPlayersModel(this)), m_timeline(new MatchTimelineModel(this)),
      m_board(new TacticsBoardModel(this)) {
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

QString MatchDayViewModel::formatText() const {
    return m_state && m_state->clock ? MatchDayTexts::formatText(m_state->clock->format)
                                     : QString{};
}

bool MatchDayViewModel::isLive() const {
    return m_state && m_state->clock && m_state->match.match.status == core::MatchStatus::Live;
}

bool MatchDayViewModel::guardLeaving() const { return isLive() && !readOnly(); }

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

QVariantList MatchDayViewModel::formatOptions() const {
    QVariantList options;
    for (const core::MatchFormat preset : core::kMatchFormatPresets) {
        options.append(QVariantMap{{QStringLiteral("value"), formatKey(preset)},
                                   {QStringLiteral("text"), MatchDayTexts::formatText(preset)}});
    }
    options.append(QVariantMap{{QStringLiteral("value"), QStringLiteral("custom")},
                               {QStringLiteral("text"), tr("Custom…")}});
    return options;
}

QString MatchDayViewModel::defaultFormat() const {
    const core::MatchFormat format =
        m_startCheck
            ? m_startCheck->defaultFormat
            : (m_state ? core::defaultMatchFormat(m_state->category) : core::MatchFormat{});
    return formatKey(format);
}

bool MatchDayViewModel::defaultClockless() const {
    return m_state && m_state->match.match.date < m_clock.today();
}

bool MatchDayViewModel::clockless() const {
    return m_state && m_state->clock && m_state->clock->clockless;
}

int MatchDayViewModel::minPeriods() { return core::kMinPeriods; }

int MatchDayViewModel::maxPeriods() { return core::kMaxPeriods; }

int MatchDayViewModel::minPeriodLength() { return core::kMinPeriodLength; }

int MatchDayViewModel::maxPeriodLength() { return core::kMaxPeriodLength; }

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
    if (clockless() && currentPhase() != core::MatchPhase::Finished) {
        return tr("Without the clock");
    }
    const auto now = reading();
    return now ? MatchDayTexts::phaseText(*now, m_state->clock->format.periods) : QString{};
}

QString MatchDayViewModel::clockText() const {
    if (clockless() && currentPhase() != core::MatchPhase::Finished) {
        return QStringLiteral("—");
    }
    const auto now = reading();
    return now ? MatchDayTexts::clockText(*now, m_state->clock->format.periodLength)
               : QStringLiteral("00:00");
}

QString MatchDayViewModel::clockActionText() const {
    if (!m_state || !m_state->clock) {
        return {};
    }
    return MatchDayTexts::clockActionText(core::nextStep(*m_state->clock),
                                          m_state->clock->format.periods);
}

bool MatchDayViewModel::canAdvanceClock() const {
    if (!isLive() || readOnly() || clockless()) {
        return false;
    }
    // With fewer than 7 only the end of the match: its own button, or the end of the last part.
    const auto step = core::nextStep(*m_state->clock);
    const bool last = step && step->kind == core::ClockStepKind::End &&
                      step->period == m_state->clock->format.periods;
    return !tooFewPlayers() || last;
}

bool MatchDayViewModel::canCancelStart() const {
    return isLive() && !readOnly() &&
           (currentPhase() == core::MatchPhase::NotStarted ||
            (clockless() && m_state->events.empty() && m_state->notes.empty() &&
             m_state->tacticChanges.empty()));
}

bool MatchDayViewModel::canSetClock() const {
    return isLive() && !readOnly() && !clockless() && currentPhase() == core::MatchPhase::Running;
}

bool MatchDayViewModel::canEndMatch() const {
    return isLive() && !readOnly() && (clockless() || tooFewPlayers());
}

QString MatchDayViewModel::endMinute() const {
    if (!m_state || !clockless() || m_state->events.empty()) {
        return {};
    }
    int minute = 0;
    for (const core::MatchEvent& event : m_state->events) {
        minute = std::max(minute, event.minute);
    }
    return QString::number(minute + 1);
}

bool MatchDayViewModel::canAddNote() const {
    const core::MatchPhase phase = currentPhase();
    return isLive() && !readOnly() &&
           (phase == core::MatchPhase::Running || phase == core::MatchPhase::Break);
}

bool MatchDayViewModel::canRecordEvents() const { return canAddNote() && !tooFewPlayers(); }

bool MatchDayViewModel::canSubstitute() const { return canRecordEvents(); }

int MatchDayViewModel::onPitchCount() const {
    return m_state ? core::onPitchCount(m_state->situation) : 0;
}

int MatchDayViewModel::minPlayers() { return core::kMinPlayersOnPitch; }

bool MatchDayViewModel::tooFewPlayers() const {
    return isLive() && onPitchCount() < core::kMinPlayersOnPitch;
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

QVariantList MatchDayViewModel::notePlayerOptions() const {
    QVariantList options{
        QVariantMap{{QStringLiteral("value"), 0}, {QStringLiteral("text"), tr("No player")}}};
    if (!m_state) {
        return options;
    }
    for (const core::PlayerInMatch& player : m_state->situation.players) {
        QString text = playerName(player.player);
        const auto found = std::ranges::find(m_state->players, player.player, &core::Player::id);
        if (found != m_state->players.end() && found->shirtNumber) {
            text = QStringLiteral("%1 %2").arg(*found->shirtNumber).arg(text);
        }
        options.append(
            QVariantMap{{QStringLiteral("value"), QVariant::fromValue(player.player.value)},
                        {QStringLiteral("text"), text}});
    }
    return options;
}

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
    case Pending::SubConfirm:
        return QStringLiteral("subConfirm");
    case Pending::OpponentGoal:
        return QStringLiteral("opponentGoal");
    case Pending::Tactic:
        return QStringLiteral("tactic");
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
    case Pending::SubConfirm:
        return tr("Substitution: %1 comes on for %2. Check the minute and save.")
            .arg(playerName(m_pendingSecond), playerName(m_pendingFirst));
    case Pending::OpponentGoal:
        return tr("Opponent goal: check the minute and save.");
    case Pending::Tactic:
        return tr("Change of shape: %1. Check the minute and confirm.")
            .arg(MatchDayTexts::tacticChangeText(
                m_state ? MatchDayTexts::shapeText(m_state->shape) : QString{}, shapeText()));
    }
    return {};
}

QString MatchDayViewModel::pendingMinute() const {
    if (m_pendingMinute) {
        return *m_pendingMinute;
    }
    // Without the clock there is no current minute: the field starts empty.
    if (!m_state || !m_state->clock || m_pending == Pending::None || clockless()) {
        return {};
    }
    return MatchDayTexts::minuteFieldText(
        core::defaultTime(*m_state->clock, m_clock.now(), pendingLikeSubstitution()));
}

QString MatchDayViewModel::pendingMinuteError() const {
    if (m_pending == Pending::None) {
        return {};
    }
    QString error;
    (void)pendingTime(error);
    return error;
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
        m_pendingSecond.reset();
        m_pendingMinute.reset();
        m_draft.reset();
        m_selectedSlot = -1;
    }
    if (otherMatch) {
        m_deletedNote.reset();
    }
    rebuildRows();
    updateTimer();
    emit matchChanged();
    emit clockChanged();
    emit eventsChanged();
    emit pendingChanged();
    emit pendingMinuteChanged();
    emit shapeChanged();
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
    m_pendingSecond.reset();
    m_pendingMinute.reset();
    m_draft.reset();
    m_selectedSlot = -1;
    m_deletedNote.reset();
    m_pitch->setRows({});
    m_bench->setRows({});
    m_timeline->setRows({});
    m_board->setRows({});
    updateTimer();
    emit matchChanged();
    emit clockChanged();
    emit eventsChanged();
    emit pendingChanged();
    emit pendingMinuteChanged();
    emit shapeChanged();
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

QString MatchDayViewModel::eventText(const core::MatchEvent& event, bool secondYellow) const {
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

QString MatchDayViewModel::actionText(const services::MatchDayAction& action) const {
    const int periods = m_state && m_state->clock ? m_state->clock->format.periods : 2;
    switch (action.kind) {
    case services::MatchDayAction::Kind::ClockStep:
        return action.step ? MatchDayTexts::clockStepName(*action.step, periods) : QString{};
    case services::MatchDayAction::Kind::Finish:
        return MatchDayTexts::finishName(m_state && m_state->clock &&
                                         m_state->clock->endedEarlyAt.has_value());
    case services::MatchDayAction::Kind::Note: {
        if (!action.note) {
            return {};
        }
        QString text = QString::fromStdString(action.note->text);
        if (text.size() > kNotePreview) {
            text = text.left(kNotePreview) + QStringLiteral("…");
        }
        return tr("%1 · %2 %3")
            .arg(MatchDayTexts::noteName(), text,
                 MatchDayTexts::minuteText(action.note->minute, action.note->addedTime));
    }
    case services::MatchDayAction::Kind::TacticChange:
        if (!action.tacticChange) {
            return {};
        }
        return tr("%1 · %2 %3")
            .arg(MatchDayTexts::tacticChangeName(), tacticChangeText(action.tacticChange->id),
                 MatchDayTexts::minuteText(action.tacticChange->minute,
                                           action.tacticChange->addedTime));
    case services::MatchDayAction::Kind::Event:
    case services::MatchDayAction::Kind::SecondYellow:
        break;
    }
    if (!action.event) {
        return {};
    }
    return eventText(*action.event, action.kind == services::MatchDayAction::Kind::SecondYellow);
}

void MatchDayViewModel::rebuildRows() {
    if (!m_state) {
        return;
    }
    const auto now = reading();
    const int currentMinute = now ? now->eventTime.minute : 0;
    m_rowsMinute = currentMinute;

    // The players of a pending action are those on the pitch (or the bench) at its minute, which
    // may be in the past (P13 re-review): the match replayed up to that time.
    std::optional<core::MatchSituation> atMinute;
    std::optional<core::MatchTime> pendingAt;
    if (m_pending != Pending::None && m_state->clock) {
        QString ignored;
        pendingAt = pendingTime(ignored);
        if (pendingAt) {
            const std::vector<core::MatchEventDraft> drafts(m_state->events.begin(),
                                                            m_state->events.end());
            if (auto replayed = core::situationAt(m_state->sheet, drafts, *pendingAt)) {
                atMinute = std::move(*replayed);
            }
        }
    }

    QList<QVariantList> pitchRows;
    QList<QVariantList> benchRows;
    QList<QVariantList> offRows;
    for (const core::PlayerInMatch& player : m_state->situation.players) {
        const auto found = std::ranges::find(m_state->players, player.player, &core::Player::id);
        const core::Player* info = found == m_state->players.end() ? nullptr : &*found;
        QString minutesText;
        if (player.minuteOn && m_state->clock) {
            const int end = player.minuteOff.value_or(currentMinute);
            minutesText = QStringLiteral("%1'").arg(std::max(0, end - *player.minuteOn));
        }
        const core::PlayerInMatch* then = atMinute ? atMinute->find(player.player) : &player;
        const core::PlayerMatchState stateThen = then ? then->state : player.state;
        bool selectable = false;
        switch (m_pending) {
        case Pending::None:
        case Pending::OpponentGoal:
        case Pending::SubConfirm:
        case Pending::Tactic:
            break;
        case Pending::Scorer:
        case Pending::Yellow:
        case Pending::Red:
        case Pending::SubOff:
            selectable = stateThen == core::PlayerMatchState::OnPitch;
            break;
        case Pending::Assist:
            selectable =
                stateThen == core::PlayerMatchState::OnPitch && player.player != m_pendingFirst;
            break;
        case Pending::SubOn:
            selectable = stateThen == core::PlayerMatchState::Bench;
            break;
        }
        // Chosen by where the player was at that minute, not where they are now: say so.
        QString historyText;
        if (selectable && pendingAt && stateThen != player.state) {
            const QString minute =
                MatchDayTexts::minuteText(pendingAt->minute, pendingAt->addedTime);
            historyText = stateThen == core::PlayerMatchState::OnPitch
                              ? tr("At %1: on the pitch").arg(minute)
                              : tr("At %1: on the bench").arg(minute);
        }
        // On the pitch: the position of the slot in the shape now (tactic changes, P13b).
        std::optional<core::Position> position = player.position;
        if (player.state == core::PlayerMatchState::OnPitch) {
            const auto slot = std::ranges::find(m_state->shape.pitchSlots,
                                                std::optional<core::PlayerId>(player.player),
                                                &core::LineupSlot::player);
            if (slot != m_state->shape.pitchSlots.end()) {
                position = slot->position;
            }
        }
        QVariantList row{QVariant::fromValue(player.player.value),
                         info ? shirtNumberValue(info->shirtNumber) : QVariant{},
                         info ? SquadTexts::displayName(*info) : QString{},
                         info ? QString::fromStdString(info->lastName) : QString{},
                         position
                             ? SquadTexts::positionCode(*position)
                             : (info ? SquadTexts::positionCode(info->primaryPosition) : QString{}),
                         MatchDayTexts::playerStateKey(player.state),
                         MatchDayTexts::playerStateName(player.state),
                         player.yellowCards,
                         player.goals,
                         minutesText,
                         selectable,
                         historyText};
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

    // Events and notes in the order of the match; the timeline shows the latest first.
    struct Entry {
        std::tuple<int, int, int> order;
        int sequence;
        QVariantList row;
    };
    std::vector<Entry> entries;
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
        entries.push_back(
            {matchOrder(event.minute, event.addedTime), static_cast<int>(entries.size()),
             QVariantList{QStringLiteral("event"), QVariant::fromValue(event.id.value), 0,
                          MatchDayTexts::minuteText(event.minute, event.addedTime),
                          MatchDayTexts::eventKey(event.type, secondYellow),
                          MatchDayTexts::eventIcon(event.type),
                          MatchDayTexts::eventName(event.type, secondYellow), playerText,
                          relatedText, QString{}, 0, QString{}}});
    }
    for (const core::MatchNote& note : m_state->notes) {
        const QString text = QString::fromStdString(note.text);
        entries.push_back(
            {matchOrder(note.minute, note.addedTime), static_cast<int>(entries.size()),
             QVariantList{QStringLiteral("note"), 0, QVariant::fromValue(note.id.value),
                          MatchDayTexts::minuteText(note.minute, note.addedTime),
                          QStringLiteral("note"), QStringLiteral("note"), MatchDayTexts::noteName(),
                          playerName(note.playerId), text,
                          MatchDayTexts::minuteFieldText({note.minute, note.addedTime}),
                          QVariant::fromValue(note.playerId ? note.playerId->value : 0), text}});
    }
    for (const core::MatchTacticChange& change : m_state->tacticChanges) {
        entries.push_back(
            {matchOrder(change.minute, change.addedTime), static_cast<int>(entries.size()),
             QVariantList{QStringLiteral("tactic"), 0, 0,
                          MatchDayTexts::minuteText(change.minute, change.addedTime),
                          QStringLiteral("tactic"), QStringLiteral("tactic"),
                          MatchDayTexts::tacticChangeName(), tacticChangeText(change.id), QString{},
                          QString{}, 0, QString{}}});
    }
    std::ranges::stable_sort(entries, [](const Entry& left, const Entry& right) {
        return std::tie(left.order, left.sequence) > std::tie(right.order, right.sequence);
    });
    QList<QVariantList> rows;
    for (Entry& entry : entries) {
        rows.append(std::move(entry.row));
    }
    m_timeline->setRows(std::move(rows));
    rebuildBoard();
}

void MatchDayViewModel::updateTimer() {
    const bool running = m_state && m_state->clock && !m_state->clock->clockless &&
                         currentPhase() == core::MatchPhase::Running;
    if (running && !m_timer.isActive()) {
        m_timer.start();
    } else if (!running) {
        m_timer.stop();
    }
}

void MatchDayViewModel::tick() {
    emit clockChanged();
    if (m_pending != Pending::None && !m_pendingMinute) {
        emit pendingMinuteChanged();
    }
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
    const int before = onPitchCount();
    if (!status) {
        report(status.error());
        // The match may have changed meanwhile (e.g. finished elsewhere): show what is stored.
        reload();
        return false;
    }
    setPending(Pending::None);
    reload();
    // The laws of the game: at least 7 players on the pitch.
    const int after = onPitchCount();
    if (isLive() && after < before) {
        if (after < core::kMinPlayersOnPitch) {
            emit tooFewPlayersReached();
        } else if (after == core::kMinPlayersOnPitch) {
            emit notice(tr("Only %1 players are left on the pitch. With fewer the match cannot go "
                           "on.")
                            .arg(after));
        }
    }
    return true;
}

bool MatchDayViewModel::start(const QString& format, int periods, int periodLength,
                              bool clockless) {
    if (!checkRunnable()) {
        return false;
    }
    core::MatchFormat chosen{periods, periodLength};
    for (const core::MatchFormat preset : core::kMatchFormatPresets) {
        if (formatKey(preset) == format) {
            chosen = preset;
        }
    }
    return finish(
        m_service.start(m_state->match.match.id, chosen, m_yellowCardThreshold, clockless));
}

bool MatchDayViewModel::cancelStart() {
    discardDraft();
    if (!checkRunnable()) {
        return false;
    }
    return finish(m_service.cancelStart(m_state->match.match.id));
}

bool MatchDayViewModel::advanceClock() {
    discardDraft();
    if (!checkRunnable()) {
        return false;
    }
    return finish(m_service.advanceClock(m_state->match.match.id));
}

bool MatchDayViewModel::endMatch(const QString& minute) {
    discardDraft();
    if (!checkRunnable()) {
        return false;
    }
    std::optional<int> at;
    if (clockless() && !minute.trimmed().isEmpty()) {
        const auto shown = core::parseDisplayMinute(minute.toStdString());
        if (!shown || shown->added) {
            emit errorOccurred(tr("Enter the minute of the end, e.g. 80."));
            return false;
        }
        // The minute shown is the minute played: the match ended after it.
        at = shown->minute;
    }
    return finish(m_service.endMatch(m_state->match.match.id, at));
}

QString MatchDayViewModel::clockTextError(const QString& text) const {
    if (!canSetClock()) {
        return {};
    }
    const auto elapsed = core::elapsedFromClockText(text.toStdString(), m_state->clock->format,
                                                    core::currentPeriod(*m_state->clock));
    if (!elapsed) {
        return tr("Enter the time as the clock shows it, e.g. 23:41, or 45+2:10 in stoppage time.");
    }
    return {};
}

QString MatchDayViewModel::setClock(const QString& text) {
    if (!checkRunnable() || !canSetClock()) {
        return {};
    }
    discardDraft();
    if (const QString error = clockTextError(text); !error.isEmpty()) {
        emit errorOccurred(error);
        return {};
    }
    const auto elapsed = core::elapsedFromClockText(text.toStdString(), m_state->clock->format,
                                                    core::currentPeriod(*m_state->clock));
    const auto before = m_service.setClock(m_state->match.match.id, *elapsed);
    if (!before) {
        report(before.error());
        reload();
        return {};
    }
    m_clockBefore = *before;
    reload();
    return tr("Clock set to %1.").arg(clockText());
}

void MatchDayViewModel::undoSetClock() {
    if (!m_clockBefore || !checkRunnable()) {
        return;
    }
    const auto restored = m_service.restoreClock(m_state->match.match.id, *m_clockBefore);
    m_clockBefore.reset();
    if (!restored) {
        report(restored.error());
    }
    reload();
}

void MatchDayViewModel::setPending(Pending pending, std::optional<core::PlayerId> first) {
    if (pending == m_pending && first == m_pendingFirst) {
        return;
    }
    // A new action starts at the current minute; its next step keeps what was typed.
    const bool sameAction = (pending == Pending::Assist && m_pending == Pending::Scorer) ||
                            (pending == Pending::SubOn && m_pending == Pending::SubOff);
    if (!sameAction) {
        m_pendingMinute.reset();
    }
    if (pending != Pending::SubConfirm) {
        m_pendingSecond.reset();
    }
    const bool draftDropped = pending != Pending::Tactic && m_draft.has_value();
    if (draftDropped) {
        m_draft.reset();
    }
    m_pending = pending;
    m_pendingFirst = first;
    rebuildRows();
    emit pendingChanged();
    emit pendingMinuteChanged();
    if (draftDropped) {
        emit shapeChanged();
    }
}

void MatchDayViewModel::beginAction(const QString& action) {
    discardDraft();
    if (!m_state || readOnly()) {
        setPending(Pending::None);
        return;
    }
    if (!canRecordEvents()) {
        setPending(Pending::None);
        return;
    }
    if (action == QStringLiteral("goal")) {
        setPending(Pending::Scorer);
    } else if (action == QStringLiteral("opponentGoal")) {
        setPending(Pending::OpponentGoal);
    } else if (action == QStringLiteral("yellow")) {
        setPending(Pending::Yellow);
    } else if (action == QStringLiteral("red")) {
        setPending(Pending::Red);
    } else if (action == QStringLiteral("sub")) {
        setPending(Pending::SubOff);
    } else {
        setPending(Pending::None);
    }
}

void MatchDayViewModel::setPendingMinute(const QString& text) {
    if (m_pending == Pending::None || m_pendingMinute == text) {
        return;
    }
    m_pendingMinute = text;
    // The players available at the minute typed.
    rebuildRows();
    emit pendingMinuteChanged();
}

std::optional<core::MatchTime> MatchDayViewModel::timeOf(const QString& text, bool substitution,
                                                         QString& error) const {
    if (!m_state || !m_state->clock) {
        return std::nullopt;
    }
    const auto shown = core::parseDisplayMinute(text.toStdString());
    if (!shown) {
        error = tr("Enter the minute as 23, or 45+2 in stoppage time.");
        return std::nullopt;
    }
    const auto time = core::matchTimeOf(*shown, m_state->clock->format);
    if (!time) {
        error = tr("This minute is outside the match.");
        return std::nullopt;
    }
    if (!core::isPlayedTime(*m_state->clock, m_clock.now(), *time, substitution)) {
        error = clockless()
                    ? tr("This minute is outside the match.")
                    : tr("This minute has not been played yet. Enter a minute up to the current "
                         "one.");
        return std::nullopt;
    }
    return time;
}

std::optional<core::MatchTime> MatchDayViewModel::pendingTime(QString& error) const {
    if (!m_state || !m_state->clock) {
        return std::nullopt;
    }
    const bool substitution = pendingLikeSubstitution();
    if (!m_pendingMinute) {
        if (clockless()) {
            error = tr("Enter the minute of the action.");
            return std::nullopt;
        }
        return core::defaultTime(*m_state->clock, m_clock.now(), substitution);
    }
    return timeOf(*m_pendingMinute, substitution, error);
}

bool MatchDayViewModel::record(core::MatchEventDraft event) {
    QString minuteError;
    const auto time = pendingTime(minuteError);
    if (!time) {
        emit errorOccurred(minuteError);
        return false;
    }
    event.minute = time->minute;
    event.addedTime = time->addedTime;
    // A late entry that a later event no longer fits: name that event (P13 manual test).
    if (const auto check = m_service.checkEvent(m_state->match.match.id, event);
        check && *check && (*check)->laterEvent) {
        const core::MatchEvent& later = *(*check)->laterEvent;
        const std::vector<core::MatchEventDraft> drafts(m_state->events.begin(),
                                                        m_state->events.end());
        const std::vector<bool> secondYellows = core::secondYellowReds(drafts);
        const auto at = std::ranges::find(m_state->events, later.id, &core::MatchEvent::id);
        const bool secondYellow =
            at != m_state->events.end() &&
            secondYellows[static_cast<std::size_t>(at - m_state->events.begin())];
        emit errorOccurred(tr("This cannot be recorded at %1: it conflicts with a later event, "
                              "%2. Undo or correct that event first.")
                               .arg(MatchDayTexts::minuteText(event.minute, event.addedTime),
                                    eventText(later, secondYellow)));
        return false;
    }
    const core::MatchId match = m_state->match.match.id;
    switch (event.type) {
    case core::MatchEventType::Goal:
        return finish(m_service.recordGoal(match, *event.playerId, event.relatedPlayerId, time));
    case core::MatchEventType::OpponentGoal:
        return finish(m_service.recordOpponentGoal(match, time));
    case core::MatchEventType::Yellow:
        return finish(m_service.recordYellow(match, *event.playerId, time));
    case core::MatchEventType::Red:
        return finish(m_service.recordRed(match, *event.playerId, time));
    case core::MatchEventType::Sub:
        return finish(
            m_service.recordSubstitution(match, *event.playerId, *event.relatedPlayerId, time));
    case core::MatchEventType::Assist:
        break;
    }
    return false;
}

bool MatchDayViewModel::choosePlayer(qint64 playerId) {
    if (!checkRunnable() || m_pending == Pending::None) {
        return false;
    }
    const core::PlayerId player{playerId};
    core::MatchEventDraft event;
    switch (m_pending) {
    case Pending::None:
    case Pending::OpponentGoal:
        return false;
    case Pending::Scorer:
        setPending(Pending::Assist, player);
        return true;
    case Pending::Assist:
        event.type = core::MatchEventType::Goal;
        event.playerId = m_pendingFirst;
        event.relatedPlayerId = player;
        return record(event);
    case Pending::Yellow:
        event.type = core::MatchEventType::Yellow;
        event.playerId = player;
        return record(event);
    case Pending::Red:
        event.type = core::MatchEventType::Red;
        event.playerId = player;
        return record(event);
    case Pending::SubOff:
        setPending(Pending::SubOn, player);
        return true;
    case Pending::SubOn:
        event.type = core::MatchEventType::Sub;
        event.playerId = player;
        event.relatedPlayerId = m_pendingFirst;
        return record(event);
    }
    return false;
}

bool MatchDayViewModel::skipAssist() {
    if (!checkRunnable() || m_pending != Pending::Assist || !m_pendingFirst) {
        return false;
    }
    core::MatchEventDraft goal;
    goal.type = core::MatchEventType::Goal;
    goal.playerId = m_pendingFirst;
    return record(goal);
}

bool MatchDayViewModel::confirmAction() {
    if (!checkRunnable()) {
        return false;
    }
    switch (m_pending) {
    case Pending::OpponentGoal: {
        core::MatchEventDraft goal;
        goal.type = core::MatchEventType::OpponentGoal;
        return record(goal);
    }
    case Pending::SubConfirm: {
        if (!m_pendingFirst || !m_pendingSecond) {
            return false;
        }
        core::MatchEventDraft sub;
        sub.type = core::MatchEventType::Sub;
        sub.playerId = m_pendingSecond;
        sub.relatedPlayerId = m_pendingFirst;
        return record(sub);
    }
    case Pending::Tactic:
        return confirmTactics();
    case Pending::None:
    case Pending::Scorer:
    case Pending::Assist:
    case Pending::Yellow:
    case Pending::Red:
    case Pending::SubOff:
    case Pending::SubOn:
        break;
    }
    return false;
}

void MatchDayViewModel::cancelAction() { setPending(Pending::None); }

QString MatchDayViewModel::undo() {
    if (!checkRunnable()) {
        return {};
    }
    discardDraft();
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

QString MatchDayViewModel::defaultNoteMinute() const {
    if (!m_state || !m_state->clock || clockless()) {
        return {};
    }
    return MatchDayTexts::minuteFieldText(core::defaultTime(*m_state->clock, m_clock.now(), false));
}

QString MatchDayViewModel::noteMinuteError(const QString& text) const {
    QString error;
    (void)timeOf(text, false, error);
    return error;
}

bool MatchDayViewModel::addNote(const QString& minute, qint64 playerId, const QString& text) {
    discardDraft();
    if (!checkRunnable()) {
        return false;
    }
    QString error;
    const auto time = timeOf(minute, false, error);
    if (!time) {
        emit errorOccurred(error);
        return false;
    }
    const std::optional<core::PlayerId> player =
        playerId > 0 ? std::optional{core::PlayerId{playerId}} : std::nullopt;
    const auto added = m_service.addNote(m_state->match.match.id, time, player, text.toStdString());
    if (!added) {
        report(added.error());
        return false;
    }
    reload();
    return true;
}

bool MatchDayViewModel::updateNote(qint64 noteId, const QString& minute, qint64 playerId,
                                   const QString& text) {
    if (!checkRunnable()) {
        return false;
    }
    discardDraft();
    QString error;
    const auto time = timeOf(minute, false, error);
    if (!time) {
        emit errorOccurred(error);
        return false;
    }
    const std::optional<core::PlayerId> player =
        playerId > 0 ? std::optional{core::PlayerId{playerId}} : std::nullopt;
    const auto updated = m_service.updateNote(m_state->match.match.id, core::MatchNoteId{noteId},
                                              *time, player, text.toStdString());
    if (!updated) {
        report(updated.error());
        return false;
    }
    reload();
    return true;
}

QString MatchDayViewModel::deleteNote(qint64 noteId) {
    discardDraft();
    if (!checkRunnable()) {
        return {};
    }
    const auto deleted = m_service.deleteNote(m_state->match.match.id, core::MatchNoteId{noteId});
    if (!deleted) {
        report(deleted.error());
        reload();
        return {};
    }
    m_deletedNote = *deleted;
    reload();
    return tr("Note deleted: %1")
        .arg(MatchDayTexts::minuteText(deleted->minute, deleted->addedTime));
}

void MatchDayViewModel::restoreDeletedNote() {
    if (!m_deletedNote || !checkRunnable()) {
        return;
    }
    const auto restored = m_service.restoreNote(*m_deletedNote);
    m_deletedNote.reset();
    if (!restored) {
        report(restored.error());
    }
    reload();
}

// --- Live tactics (packet P13b) ---

TacticsBoardModel* MatchDayViewModel::board() const { return m_board; }

QVariantList MatchDayViewModel::zones() const {
    QVariantList zones;
    for (const core::PitchZone& zone : core::pitchZones()) {
        zones.append(QVariantMap{
            {QStringLiteral("position"), QString::fromUtf8(core::toDbText(zone.position))},
            {QStringLiteral("text"), SquadTexts::positionCode(zone.position)},
            {QStringLiteral("x"), zone.left},
            {QStringLiteral("y"), zone.top},
            {QStringLiteral("width"), zone.width},
            {QStringLiteral("height"), zone.height},
        });
    }
    return zones;
}

QVariantList MatchDayViewModel::formationOptions() const {
    QVariantList options;
    if (m_state && shapeFormation() == kCustomShape) {
        options.append(choice(kCustomShape, shapeText()));
    }
    for (const core::Formation& formation : core::formations()) {
        const QString name = QString::fromUtf8(formation.name);
        options.append(choice(name, name));
    }
    return options;
}

QVariantList MatchDayViewModel::positionOptions() const { return SquadTexts::positionOptions(); }

const core::Lineup& MatchDayViewModel::boardShape() const {
    static const core::Lineup kNone;
    if (m_draft) {
        return *m_draft;
    }
    return m_state ? m_state->shape : kNone;
}

QString MatchDayViewModel::shapeText() const {
    return m_state && m_state->hasLineup ? MatchDayTexts::shapeText(boardShape()) : QString{};
}

QString MatchDayViewModel::shapeFormation() const {
    const core::Lineup& shape = boardShape();
    if (std::ranges::count_if(shape.pitchSlots, [](const core::LineupSlot& slot) {
            return slot.player.has_value();
        }) == core::kLineupSlots) {
        if (const core::Formation* formation = core::matchingFormation(shape.pitchSlots)) {
            return QString::fromUtf8(formation->name);
        }
    }
    return kCustomShape;
}

bool MatchDayViewModel::canChangeTactics() const { return canSubstitute() && m_state->hasLineup; }

bool MatchDayViewModel::tacticsDirty() const { return m_draft.has_value(); }

int MatchDayViewModel::selectedSlot() const { return m_selectedSlot; }

QString MatchDayViewModel::selectedPosition() const {
    const core::Lineup& shape = boardShape();
    if (!core::isValidSlot(shape, m_selectedSlot)) {
        return {};
    }
    return QString::fromUtf8(
        core::toDbText(shape.pitchSlots[static_cast<std::size_t>(m_selectedSlot)].position));
}

bool MatchDayViewModel::noGoalkeeper() const {
    return m_state && m_state->hasLineup && m_state->clock &&
           !core::hasGoalkeeperOnPitch(boardShape());
}

bool MatchDayViewModel::pendingLikeSubstitution() const {
    return m_pending == Pending::SubOff || m_pending == Pending::SubOn ||
           m_pending == Pending::SubConfirm || m_pending == Pending::Tactic;
}

void MatchDayViewModel::rebuildBoard() {
    if (!m_state || !m_state->hasLineup) {
        m_board->setRows({});
        return;
    }
    const core::Lineup& shape = boardShape();
    QList<QVariantList> rows;
    for (std::size_t i = 0; i < shape.pitchSlots.size(); ++i) {
        const core::LineupSlot& slot = shape.pitchSlots[i];
        const core::Player* info = nullptr;
        const core::PlayerInMatch* inMatch = nullptr;
        if (slot.player) {
            const auto found = std::ranges::find(m_state->players, *slot.player, &core::Player::id);
            info = found == m_state->players.end() ? nullptr : &*found;
            inMatch = m_state->situation.find(*slot.player);
        }
        rows.append(
            QVariantList{static_cast<int>(i), QString::fromUtf8(core::toDbText(slot.position)),
                         SquadTexts::positionCode(slot.position), slot.x, slot.y,
                         QVariant::fromValue(slot.player ? slot.player->value : 0),
                         info ? shirtNumberValue(info->shirtNumber) : QVariant{},
                         info ? QString::fromStdString(info->lastName) : QString{},
                         info ? SquadTexts::displayName(*info) : QString{}, QString{}, QString{},
                         static_cast<int>(i) == m_selectedSlot, inMatch ? inMatch->yellowCards : 0,
                         inMatch ? inMatch->goals : 0, !slot.player.has_value()});
    }
    m_board->setRows(std::move(rows));
}

bool MatchDayViewModel::applyDraft(core::Lineup edited, core::LineupEdit edit) {
    switch (edit) {
    case core::LineupEdit::Done:
        break;
    case core::LineupEdit::GoalkeeperTaken:
        emit errorOccurred(tr("Only one player can be in goal. Move the goalkeeper out first."));
        return false;
    case core::LineupEdit::Unchanged:
    case core::LineupEdit::BenchFull:
    case core::LineupEdit::InvalidSlot:
        return false;
    }
    if (edited == m_state->shape) {
        // Back to the shape on the pitch: nothing to confirm.
        setPending(Pending::None);
        rebuildBoard();
        emit shapeChanged();
        return true;
    }
    if (m_pending != Pending::Tactic) {
        setPending(Pending::None);
        m_draft = std::move(edited);
        setPending(Pending::Tactic);
    } else {
        m_draft = std::move(edited);
        emit pendingChanged();
    }
    rebuildBoard();
    emit shapeChanged();
    return true;
}

bool MatchDayViewModel::changeFormation(const QString& name) {
    const core::Formation* target = core::findFormation(name.toStdString());
    if (!target || !checkRunnable() || !canChangeTactics()) {
        return false;
    }
    core::Lineup edited = core::changeShape(boardShape(), *target);
    const core::LineupEdit edit =
        edited == boardShape() ? core::LineupEdit::Unchanged : core::LineupEdit::Done;
    return applyDraft(std::move(edited), edit);
}

bool MatchDayViewModel::moveSlotTo(int slot, double x, double y) {
    if (!checkRunnable() || !canChangeTactics()) {
        return false;
    }
    core::Lineup edited = boardShape();
    const core::LineupEdit edit = core::moveLiveSlot(edited, slot, core::positionAt(x, y), x);
    return applyDraft(std::move(edited), edit);
}

bool MatchDayViewModel::setSlotPosition(int slot, const QString& position) {
    const auto parsed = core::fromDbText<core::Position>(position.toStdString());
    if (!parsed) {
        qWarning("MatchDayViewModel: unknown position '%s'", qUtf8Printable(position));
        return false;
    }
    if (!checkRunnable() || !canChangeTactics()) {
        return false;
    }
    core::Lineup edited = boardShape();
    const core::LineupEdit edit = core::moveLiveSlot(edited, slot, *parsed);
    const bool changed = applyDraft(std::move(edited), edit);
    if (!changed) {
        // A refused position (a second goalkeeper) shows the current one again (P12 review).
        emit shapeChanged();
    }
    return changed;
}

bool MatchDayViewModel::swapSlots(int first, int second) {
    if (!checkRunnable() || !canChangeTactics()) {
        return false;
    }
    core::Lineup edited = boardShape();
    const core::LineupEdit edit = core::swapPlaces(edited, first, second);
    return applyDraft(std::move(edited), edit);
}

void MatchDayViewModel::selectSlot(int slot) {
    const int selected = core::isValidSlot(boardShape(), slot) ? slot : -1;
    if (selected == m_selectedSlot) {
        return;
    }
    m_selectedSlot = selected;
    rebuildBoard();
    emit shapeChanged();
}

bool MatchDayViewModel::substituteOnBoard(int slot, qint64 playerId) {
    if (!checkRunnable() || !canSubstitute()) {
        return false;
    }
    if (m_draft) {
        emit errorOccurred(tr("Confirm or cancel the change of shape first, then make the "
                              "substitution."));
        return false;
    }
    const core::Lineup& shape = boardShape();
    if (!core::isValidSlot(shape, slot)) {
        return false;
    }
    const std::optional<core::PlayerId> goingOff =
        shape.pitchSlots[static_cast<std::size_t>(slot)].player;
    const core::PlayerId comingOn{playerId};
    const core::PlayerInMatch* substitute = m_state->situation.find(comingOn);
    if (!goingOff || substitute == nullptr || substitute->state != core::PlayerMatchState::Bench) {
        emit errorOccurred(tr("Only a player on the bench can come on."));
        return false;
    }
    setPending(Pending::None);
    m_pendingSecond = comingOn;
    setPending(Pending::SubConfirm, goingOff);
    return true;
}

void MatchDayViewModel::discardDraft() {
    if (!m_draft) {
        return;
    }
    setPending(Pending::None);
    emit notice(tr("The unconfirmed change of shape was discarded."));
}

bool MatchDayViewModel::confirmTactics() {
    if (!m_draft) {
        return false;
    }
    QString minuteError;
    const auto time = pendingTime(minuteError);
    if (!time) {
        emit errorOccurred(minuteError);
        return false;
    }
    const auto recorded = m_service.recordTacticChange(m_state->match.match.id, *m_draft, time);
    if (!recorded && recorded.error().field == "tactic_change_later" &&
        !m_state->tacticChanges.empty()) {
        // Name the change that is later (P13b decision 5).
        qWarning("MatchDayViewModel: %s", recorded.error().detail.c_str());
        const core::MatchTacticChange& last = m_state->tacticChanges.back();
        emit errorOccurred(tr("A later change of shape is already recorded: %1 at %2. A change can "
                              "be entered only at or after that minute.")
                               .arg(tacticChangeText(last.id),
                                    MatchDayTexts::minuteText(last.minute, last.addedTime)));
        return false;
    }
    return finish(recorded ? core::Status{} : core::Status{std::unexpected(recorded.error())});
}

QString MatchDayViewModel::tacticChangeText(core::MatchTacticChangeId change) const {
    if (!m_state) {
        return {};
    }
    const auto found =
        std::ranges::find(m_state->tacticChanges, change, &core::MatchTacticChange::id);
    if (found == m_state->tacticChanges.end()) {
        return {};
    }
    const std::vector<core::MatchEventDraft> events(m_state->events.begin(), m_state->events.end());
    const std::vector<core::MatchTacticChangeDraft> changes(m_state->tacticChanges.begin(),
                                                            m_state->tacticChanges.end());
    const auto names = core::shapeChangeNames(m_state->lineup, events, changes);
    if (!names) {
        return QString::fromStdString(found->shape.formation);
    }
    const auto& entry = (*names)[static_cast<std::size_t>(found - m_state->tacticChanges.begin())];
    return MatchDayTexts::tacticChangeText(QString::fromStdString(entry.from),
                                           QString::fromStdString(entry.to));
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
        emit pendingMinuteChanged();
        emit loadErrorChanged();
    }
    return QObject::eventFilter(watched, event);
}

} // namespace fdss::viewmodels
