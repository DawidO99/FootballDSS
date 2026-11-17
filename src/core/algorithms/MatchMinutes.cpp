#include "core/algorithms/MatchMinutes.hpp"

#include <algorithm>
#include <map>
#include <numeric>
#include <ranges>

namespace fdss::core {

namespace {

using std::chrono::seconds;

constexpr seconds kMinute{60};

seconds elapsedSince(Timestamp start, Timestamp now) noexcept {
    return std::max(seconds{0}, now - start);
}

// One half: regular time up to `halfEnd` match minutes, then stoppage time.
void readHalf(ClockReading& reading, int halfStartMinute, int halfLength,
              seconds elapsed) noexcept {
    const seconds length = halfLength * kMinute;
    reading.regular = halfStartMinute * kMinute + std::min(elapsed, length);
    reading.stoppage = std::max(seconds{0}, elapsed - length);
    const auto fullMinutes = static_cast<int>(elapsed / kMinute);
    if (fullMinutes >= halfLength) {
        reading.eventTime = {halfStartMinute + halfLength, fullMinutes - halfLength + 1};
    } else {
        reading.eventTime = {halfStartMinute + fullMinutes, std::nullopt};
    }
}

bool isOnPitch(const PlayerInMatch* player) noexcept {
    return player != nullptr && player->state == PlayerMatchState::OnPitch;
}

// Why a player cannot act on the pitch; nullopt when they are on it.
std::optional<MatchEventProblem> notOnPitchReason(const PlayerInMatch* player) noexcept {
    if (player == nullptr) {
        return MatchEventProblem::UnknownPlayer;
    }
    switch (player->state) {
    case PlayerMatchState::OnPitch:
        return std::nullopt;
    case PlayerMatchState::Bench:
        return MatchEventProblem::NotOnPitch;
    case PlayerMatchState::SubstitutedOff:
        return MatchEventProblem::AlreadySubstituted;
    case PlayerMatchState::SentOff:
        return MatchEventProblem::SentOff;
    }
    return MatchEventProblem::NotOnPitch;
}

std::optional<MatchEventProblem> comingOnReason(const PlayerInMatch* player) noexcept {
    if (player == nullptr) {
        return MatchEventProblem::UnknownPlayer;
    }
    switch (player->state) {
    case PlayerMatchState::Bench:
        return std::nullopt;
    case PlayerMatchState::OnPitch:
        return MatchEventProblem::NotOnBench;
    case PlayerMatchState::SubstitutedOff:
        return MatchEventProblem::AlreadySubstituted;
    case PlayerMatchState::SentOff:
        return MatchEventProblem::SentOff;
    }
    return MatchEventProblem::NotOnBench;
}

} // namespace

int defaultHalfLength(TeamCategory category) noexcept {
    switch (category) {
    case TeamCategory::Senior:
    case TeamCategory::U19:
    case TeamCategory::Other:
        return 45;
    case TeamCategory::U17:
        return 40;
    case TeamCategory::U15:
        return 35;
    case TeamCategory::U13:
        return 30;
    case TeamCategory::U11:
        return 25;
    }
    return 45;
}

MatchPhase phaseOf(const MatchClock& clock) noexcept {
    if (clock.matchEnd) {
        return MatchPhase::Finished;
    }
    if (clock.secondHalfStart) {
        return MatchPhase::SecondHalf;
    }
    if (clock.firstHalfEnd) {
        return MatchPhase::HalfTime;
    }
    if (clock.firstHalfStart) {
        return MatchPhase::FirstHalf;
    }
    return MatchPhase::NotStarted;
}

std::optional<ClockStep> nextStep(const MatchClock& clock) noexcept {
    switch (phaseOf(clock)) {
    case MatchPhase::NotStarted:
        return ClockStep::FirstHalfStart;
    case MatchPhase::FirstHalf:
        return ClockStep::FirstHalfEnd;
    case MatchPhase::HalfTime:
        return ClockStep::SecondHalfStart;
    case MatchPhase::SecondHalf:
        return ClockStep::MatchEnd;
    case MatchPhase::Finished:
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<ClockStep> lastStep(const MatchClock& clock) noexcept {
    switch (phaseOf(clock)) {
    case MatchPhase::NotStarted:
        return std::nullopt;
    case MatchPhase::FirstHalf:
        return ClockStep::FirstHalfStart;
    case MatchPhase::HalfTime:
        return ClockStep::FirstHalfEnd;
    case MatchPhase::SecondHalf:
        return ClockStep::SecondHalfStart;
    case MatchPhase::Finished:
        return ClockStep::MatchEnd;
    }
    return std::nullopt;
}

void setStep(MatchClock& clock, ClockStep step, std::optional<Timestamp> at) noexcept {
    switch (step) {
    case ClockStep::FirstHalfStart:
        clock.firstHalfStart = at;
        return;
    case ClockStep::FirstHalfEnd:
        clock.firstHalfEnd = at;
        return;
    case ClockStep::SecondHalfStart:
        clock.secondHalfStart = at;
        return;
    case ClockStep::MatchEnd:
        clock.matchEnd = at;
        return;
    }
}

DisplayMinute displayMinute(const MatchTime& time) noexcept {
    if (time.addedTime) {
        return {time.minute, time.addedTime};
    }
    return {time.minute + 1, std::nullopt};
}

ClockReading readClock(const MatchClock& clock, Timestamp now) noexcept {
    ClockReading reading;
    reading.phase = phaseOf(clock);
    const int half = clock.halfLength;
    switch (reading.phase) {
    case MatchPhase::NotStarted:
        break;
    case MatchPhase::FirstHalf:
        readHalf(reading, 0, half, elapsedSince(*clock.firstHalfStart, now));
        break;
    case MatchPhase::HalfTime:
        reading.regular = half * kMinute;
        reading.eventTime = {half, std::nullopt};
        break;
    case MatchPhase::SecondHalf:
        readHalf(reading, half, half, elapsedSince(*clock.secondHalfStart, now));
        break;
    case MatchPhase::Finished:
        reading.regular = 2 * half * kMinute;
        reading.eventTime = {2 * half, std::nullopt};
        break;
    }
    return reading;
}

MatchSheet matchSheetOf(const Lineup& lineup) {
    MatchSheet sheet;
    for (const LineupSlot& slot : lineup.pitchSlots) {
        if (slot.player) {
            sheet.starters.push_back({*slot.player, slot.position});
        }
    }
    sheet.bench = lineup.bench;
    return sheet;
}

const PlayerInMatch* MatchSituation::find(PlayerId player) const noexcept {
    const auto found = std::ranges::find(players, player, &PlayerInMatch::player);
    return found == players.end() ? nullptr : &*found;
}

PlayerInMatch* MatchSituation::find(PlayerId player) noexcept {
    const auto found = std::ranges::find(players, player, &PlayerInMatch::player);
    return found == players.end() ? nullptr : &*found;
}

MatchSituation startingSituation(const MatchSheet& sheet) {
    MatchSituation situation;
    for (const StartingPlayer& starter : sheet.starters) {
        PlayerInMatch player;
        player.player = starter.player;
        player.state = PlayerMatchState::OnPitch;
        player.started = true;
        player.minuteOn = 0;
        player.position = starter.position;
        situation.players.push_back(player);
    }
    for (const PlayerId player : sheet.bench) {
        if (situation.find(player) == nullptr) {
            PlayerInMatch substitute;
            substitute.player = player;
            situation.players.push_back(substitute);
        }
    }
    return situation;
}

std::optional<MatchEventProblem> checkEvent(const MatchSituation& situation,
                                            const MatchEventDraft& event) {
    MatchSituation copy = situation;
    return applyEvent(copy, event);
}

std::optional<MatchEventProblem> applyEvent(MatchSituation& situation,
                                            const MatchEventDraft& event) {
    if (event.minute < 0 || event.minute > kMaxMatchMinute ||
        (event.addedTime && *event.addedTime < 1)) {
        return MatchEventProblem::MinuteOutOfRange;
    }
    if (event.type == MatchEventType::OpponentGoal) {
        ++situation.goalsAgainst;
        return std::nullopt;
    }
    if (!event.playerId) {
        return MatchEventProblem::MissingPlayer;
    }
    PlayerInMatch* player = situation.find(*event.playerId);
    switch (event.type) {
    case MatchEventType::Goal: {
        if (auto problem = notOnPitchReason(player)) {
            return problem;
        }
        PlayerInMatch* assistant = nullptr;
        if (event.relatedPlayerId) {
            if (*event.relatedPlayerId == *event.playerId) {
                return MatchEventProblem::SamePlayer;
            }
            assistant = situation.find(*event.relatedPlayerId);
            if (auto problem = notOnPitchReason(assistant)) {
                return problem;
            }
        }
        ++player->goals;
        if (assistant != nullptr) {
            ++assistant->assists;
        }
        ++situation.goalsFor;
        return std::nullopt;
    }
    case MatchEventType::Assist:
        // Match Day never writes these (the assistant is on the goal); kept for imported data.
        if (player == nullptr) {
            return MatchEventProblem::UnknownPlayer;
        }
        ++player->assists;
        return std::nullopt;
    case MatchEventType::Yellow:
        if (auto problem = notOnPitchReason(player)) {
            return problem;
        }
        ++player->yellowCards;
        return std::nullopt;
    case MatchEventType::Red:
        if (auto problem = notOnPitchReason(player)) {
            return problem;
        }
        player->state = PlayerMatchState::SentOff;
        player->minuteOff = event.minute;
        return std::nullopt;
    case MatchEventType::Sub: {
        if (!event.relatedPlayerId) {
            return MatchEventProblem::MissingPlayer;
        }
        if (*event.relatedPlayerId == *event.playerId) {
            return MatchEventProblem::SamePlayer;
        }
        PlayerInMatch* goingOff = situation.find(*event.relatedPlayerId);
        if (auto problem = notOnPitchReason(goingOff)) {
            return problem;
        }
        if (auto problem = comingOnReason(player)) {
            return problem;
        }
        goingOff->state = PlayerMatchState::SubstitutedOff;
        goingOff->minuteOff = event.minute;
        player->state = PlayerMatchState::OnPitch;
        player->minuteOn = event.minute;
        player->position = goingOff->position;
        return std::nullopt;
    }
    case MatchEventType::OpponentGoal:
        break;
    }
    return std::nullopt;
}

std::vector<std::size_t> replayOrder(std::span<const MatchEventDraft> events) {
    std::vector<std::size_t> order(events.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::stable_sort(order, {}, [&](std::size_t index) { return events[index].minute; });
    return order;
}

std::vector<bool> secondYellowReds(std::span<const MatchEventDraft> events) {
    std::vector<bool> flags(events.size(), false);
    std::map<PlayerId, int> yellows;
    for (const std::size_t index : replayOrder(events)) {
        const MatchEventDraft& event = events[index];
        if (!event.playerId) {
            continue;
        }
        if (event.type == MatchEventType::Yellow) {
            ++yellows[*event.playerId];
        } else if (event.type == MatchEventType::Red && !event.relatedPlayerId) {
            flags[index] = yellows[*event.playerId] >= 2;
        }
    }
    return flags;
}

std::expected<MatchSituation, MatchEventError> replayMatch(const MatchSheet& sheet,
                                                           std::span<const MatchEventDraft> events,
                                                           std::optional<int> finalMinute) {
    const std::vector<std::size_t> order = replayOrder(events);
    MatchSituation situation = startingSituation(sheet);
    for (const std::size_t index : order) {
        const MatchEventDraft& event = events[index];
        if (finalMinute && event.minute > *finalMinute) {
            return std::unexpected(MatchEventError{MatchEventProblem::AfterFinalMinute, index});
        }
        if (auto problem = applyEvent(situation, event)) {
            return std::unexpected(MatchEventError{*problem, index});
        }
    }
    return situation;
}

MatchMinutes minutesOf(const MatchSituation& situation, std::optional<int> finalMinute) {
    MatchMinutes minutes;
    minutes.goalsFor = situation.goalsFor;
    minutes.goalsAgainst = situation.goalsAgainst;
    for (const PlayerInMatch& player : situation.players) {
        if (!player.minuteOn) {
            continue;
        }
        AppearanceDraft appearance;
        appearance.playerId = player.player;
        appearance.started = player.started;
        appearance.minuteOn = player.minuteOn;
        appearance.minuteOff = player.minuteOff;
        if (isOnPitch(&player)) {
            appearance.minuteOff = finalMinute;
        }
        appearance.position = player.position;
        minutes.appearances.push_back(appearance);
    }
    return minutes;
}

std::expected<MatchMinutes, MatchEventError>
computeMatchMinutes(const MatchSheet& sheet, std::span<const MatchEventDraft> events,
                    std::optional<int> finalMinute) {
    auto situation = replayMatch(sheet, events, finalMinute);
    if (!situation) {
        return std::unexpected(situation.error());
    }
    return minutesOf(*situation, finalMinute);
}

} // namespace fdss::core
