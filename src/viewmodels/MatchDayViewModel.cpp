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
    const auto now = reading();
    return now ? MatchDayTexts::phaseText(*now, m_state->clock->format.periods) : QString{};
}

QString MatchDayViewModel::clockText() const {
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
    if (!isLive() || readOnly()) {
        return false;
    }
    // With fewer than 7 only the end of the match: its own button, or the end of the last part.
    const auto step = core::nextStep(*m_state->clock);
    const bool last = step && step->kind == core::ClockStepKind::End &&
                      step->period == m_state->clock->format.periods;
    return !tooFewPlayers() || last;
}

bool MatchDayViewModel::canCancelStart() const {
    return isLive() && !readOnly() && currentPhase() == core::MatchPhase::NotStarted;
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
    case Pending::OpponentGoal:
        return QStringLiteral("opponentGoal");
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
    case Pending::OpponentGoal:
        return tr("Opponent goal: check the minute and save.");
    }
    return {};
}

QString MatchDayViewModel::pendingMinute() const {
    if (m_pendingMinute) {
        return *m_pendingMinute;
    }
    if (!m_state || !m_state->clock || m_pending == Pending::None) {
        return {};
    }
    const bool substitution = m_pending == Pending::SubOff || m_pending == Pending::SubOn;
    return MatchDayTexts::minuteFieldText(
        core::defaultTime(*m_state->clock, m_clock.now(), substitution));
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
        m_pendingMinute.reset();
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
    m_pendingMinute.reset();
    m_deletedNote.reset();
    m_pitch->setRows({});
    m_bench->setRows({});
    m_timeline->setRows({});
    updateTimer();
    emit matchChanged();
    emit clockChanged();
    emit eventsChanged();
    emit pendingChanged();
    emit pendingMinuteChanged();
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
        return MatchDayTexts::finishName(m_state && m_state->clock && m_state->clock->endedEarly);
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
        bool selectable = false;
        switch (m_pending) {
        case Pending::None:
        case Pending::OpponentGoal:
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
    std::ranges::stable_sort(entries, [](const Entry& left, const Entry& right) {
        return std::tie(left.order, left.sequence) > std::tie(right.order, right.sequence);
    });
    QList<QVariantList> rows;
    for (Entry& entry : entries) {
        rows.append(std::move(entry.row));
    }
    m_timeline->setRows(std::move(rows));
}

void MatchDayViewModel::updateTimer() {
    const bool running = m_state && m_state->clock && currentPhase() == core::MatchPhase::Running;
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

bool MatchDayViewModel::start(const QString& format, int periods, int periodLength) {
    if (!checkRunnable()) {
        return false;
    }
    core::MatchFormat chosen{periods, periodLength};
    for (const core::MatchFormat preset : core::kMatchFormatPresets) {
        if (formatKey(preset) == format) {
            chosen = preset;
        }
    }
    return finish(m_service.start(m_state->match.match.id, chosen, m_yellowCardThreshold));
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

bool MatchDayViewModel::endMatch() {
    if (!checkRunnable()) {
        return false;
    }
    return finish(m_service.endMatch(m_state->match.match.id));
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
    m_pending = pending;
    m_pendingFirst = first;
    rebuildRows();
    emit pendingChanged();
    emit pendingMinuteChanged();
}

void MatchDayViewModel::beginAction(const QString& action) {
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
    emit pendingMinuteChanged();
}

std::optional<core::MatchTime> MatchDayViewModel::timeOf(const QString& text, bool substitution,
                                                         QString& error) const {
    if (!m_state || !m_state->clock) {
        return std::nullopt;
    }
    const auto shown = core::parseDisplayMinute(text.toStdString());
    const auto time = shown ? core::matchTimeOf(*shown, m_state->clock->format) : std::nullopt;
    if (!time) {
        error = tr("Enter the minute as 23, or 45+2 in stoppage time.");
        return std::nullopt;
    }
    if (!core::isPlayedTime(*m_state->clock, m_clock.now(), *time, substitution)) {
        error = tr("This minute has not been played yet. Enter a minute up to the current one.");
        return std::nullopt;
    }
    return time;
}

std::optional<core::MatchTime> MatchDayViewModel::pendingTime(QString& error) const {
    if (!m_state || !m_state->clock) {
        return std::nullopt;
    }
    const bool substitution = m_pending == Pending::SubOff || m_pending == Pending::SubOn;
    if (!m_pendingMinute) {
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
    if (!checkRunnable() || m_pending != Pending::OpponentGoal) {
        return false;
    }
    core::MatchEventDraft goal;
    goal.type = core::MatchEventType::OpponentGoal;
    return record(goal);
}

void MatchDayViewModel::cancelAction() { setPending(Pending::None); }

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

QString MatchDayViewModel::defaultNoteMinute() const {
    if (!m_state || !m_state->clock) {
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
