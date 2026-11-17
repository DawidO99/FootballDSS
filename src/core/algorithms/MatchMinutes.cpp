#include "core/algorithms/MatchMinutes.hpp"

#include <algorithm>
#include <map>
#include <numeric>
#include <ranges>
#include <tuple>

namespace fdss::core {

namespace {

using std::chrono::seconds;

constexpr seconds kMinute{60};

seconds elapsedSince(Timestamp start, Timestamp now) noexcept {
    return std::max(seconds{0}, now - start);
}

int fullMinutes(seconds elapsed) noexcept { return static_cast<int>(elapsed / kMinute); }

// One running part: regular time up to its end, then stoppage time.
void readPeriod(ClockReading& reading, int period, int length, seconds elapsed) noexcept {
    const int startMinute = (period - 1) * length;
    reading.regular = startMinute * kMinute + std::min(elapsed, length * kMinute);
    reading.stoppage = std::max(seconds{0}, elapsed - length * kMinute);
    const int full = fullMinutes(elapsed);
    if (full >= length) {
        reading.eventTime = {period * length, full - length + 1};
    } else {
        reading.eventTime = {startMinute + full, std::nullopt};
    }
}

// The added minutes played at the end of a part that has ended; 0 without stoppage time.
int addedPlayed(const PeriodTimes& times, int length) noexcept {
    const int full = fullMinutes(elapsedSince(times.start, *times.end));
    return full >= length ? full - length + 1 : 0;
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

MatchFormat defaultMatchFormat(TeamCategory category) noexcept {
    switch (category) {
    case TeamCategory::Senior:
    case TeamCategory::U19:
    case TeamCategory::Other:
        return {2, 45};
    case TeamCategory::U17:
        return {2, 40};
    case TeamCategory::U15:
        return {2, 35};
    case TeamCategory::U13:
        return {2, 30};
    case TeamCategory::U11:
        return {2, 25};
    }
    return {2, 45};
}

MatchPhase phaseOf(const MatchClock& clock) noexcept {
    if (clock.finished) {
        return MatchPhase::Finished;
    }
    if (clock.periods.empty()) {
        return MatchPhase::NotStarted;
    }
    return clock.periods.back().end ? MatchPhase::Break : MatchPhase::Running;
}

int currentPeriod(const MatchClock& clock) noexcept {
    return static_cast<int>(clock.periods.size());
}

std::optional<ClockStep> nextStep(const MatchClock& clock) noexcept {
    switch (phaseOf(clock)) {
    case MatchPhase::NotStarted:
        return ClockStep{1, ClockStepKind::Start};
    case MatchPhase::Running:
        return ClockStep{currentPeriod(clock), ClockStepKind::End};
    case MatchPhase::Break:
        return ClockStep{currentPeriod(clock) + 1, ClockStepKind::Start};
    case MatchPhase::Finished:
        return std::nullopt;
    }
    return std::nullopt;
}

void applyStep(MatchClock& clock, ClockStep step, Timestamp at) {
    const int period = currentPeriod(clock);
    if (step.kind == ClockStepKind::Start && step.period == period + 1 &&
        (clock.periods.empty() || clock.periods.back().end)) {
        clock.periods.push_back({at, std::nullopt});
    } else if (step.kind == ClockStepKind::End && step.period == period && period > 0 &&
               !clock.periods.back().end) {
        clock.periods.back().end = at;
    }
}

int finalMinute(const MatchClock& clock) noexcept {
    const int length = clock.format.periodLength;
    const int played = currentPeriod(clock);
    if (played == 0) {
        return 0;
    }
    const PeriodTimes& last = clock.periods.back();
    if (!clock.endedEarly) {
        return clock.format.totalMinutes();
    }
    if (!last.end) {
        return (played - 1) * length;
    }
    // Ended early: the minute the last part ended, at most its end.
    return (played - 1) * length +
           std::min(fullMinutes(elapsedSince(last.start, *last.end)), length);
}

DisplayMinute displayMinute(const MatchTime& time) noexcept {
    if (time.addedTime) {
        return {time.minute, time.addedTime};
    }
    return {time.minute + 1, std::nullopt};
}

std::optional<DisplayMinute> parseDisplayMinute(std::string_view text) {
    const auto trim = [](std::string_view value) {
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
            value.remove_prefix(1);
        }
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
            value.remove_suffix(1);
        }
        return value;
    };
    text = trim(text);
    if (!text.empty() && text.back() == '\'') {
        text = trim(text.substr(0, text.size() - 1));
    }
    const auto number = [](std::string_view digits) -> std::optional<int> {
        if (digits.empty() || digits.size() > 3) {
            return std::nullopt;
        }
        int value = 0;
        for (const char digit : digits) {
            if (digit < '0' || digit > '9') {
                return std::nullopt;
            }
            value = value * 10 + (digit - '0');
        }
        return value;
    };
    const std::size_t plus = text.find('+');
    if (plus == std::string_view::npos) {
        const auto minute = number(text);
        if (!minute || *minute < 1) {
            return std::nullopt;
        }
        return DisplayMinute{*minute, std::nullopt};
    }
    const auto minute = number(trim(text.substr(0, plus)));
    const auto added = number(trim(text.substr(plus + 1)));
    if (!minute || !added || *minute < 1 || *added < 1) {
        return std::nullopt;
    }
    return DisplayMinute{*minute, *added};
}

std::optional<MatchTime> matchTimeOf(DisplayMinute shown, MatchFormat format) {
    if (shown.added) {
        if (shown.minute % format.periodLength != 0 ||
            shown.minute / format.periodLength > format.periods || *shown.added < 1) {
            return std::nullopt;
        }
        return MatchTime{shown.minute, shown.added};
    }
    if (shown.minute < 1 || shown.minute > format.totalMinutes()) {
        return std::nullopt;
    }
    return MatchTime{shown.minute - 1, std::nullopt};
}

ClockReading readClock(const MatchClock& clock, Timestamp now) noexcept {
    ClockReading reading;
    reading.phase = phaseOf(clock);
    reading.period = currentPeriod(clock);
    const int length = clock.format.periodLength;
    switch (reading.phase) {
    case MatchPhase::NotStarted:
        break;
    case MatchPhase::Running:
        readPeriod(reading, reading.period, length, elapsedSince(clock.periods.back().start, now));
        break;
    case MatchPhase::Break:
        reading.regular = reading.period * length * kMinute;
        reading.eventTime = {reading.period * length, std::nullopt};
        break;
    case MatchPhase::Finished: {
        const int last = finalMinute(clock);
        reading.regular = last * kMinute;
        reading.eventTime = {last, std::nullopt};
        break;
    }
    }
    return reading;
}

bool isPlayedTime(const MatchClock& clock, Timestamp now, MatchTime time, bool substitution) {
    const int length = clock.format.periodLength;
    const MatchPhase phase = phaseOf(clock);
    const int played = currentPeriod(clock);
    if (time.minute < 0 || played == 0) {
        return false;
    }
    const ClockReading reading = readClock(clock, now);
    if (time.addedTime) {
        const int period = time.minute / length;
        if (time.minute % length != 0 || period < 1 || period > played || *time.addedTime < 1) {
            return false;
        }
        const PeriodTimes& times = clock.periods[static_cast<std::size_t>(period - 1)];
        if (!times.end) {
            return reading.eventTime.addedTime && *time.addedTime <= *reading.eventTime.addedTime;
        }
        return *time.addedTime <= addedPlayed(times, length);
    }
    const int period = time.minute / length + 1;
    if (period > clock.format.periods) {
        return false;
    }
    if (period > played) {
        // The minute that starts the next part: only a substitution in the break before it.
        return substitution && phase == MatchPhase::Break && period == played + 1 &&
               time.minute == played * length;
    }
    const PeriodTimes& times = clock.periods[static_cast<std::size_t>(period - 1)];
    if (!times.end) {
        return reading.eventTime.addedTime || time.minute <= reading.eventTime.minute;
    }
    // An ended part: its minutes as far as they were played.
    return time.minute - (period - 1) * length <=
           std::min(fullMinutes(elapsedSince(times.start, *times.end)), length - 1);
}

MatchTime defaultTime(const MatchClock& clock, Timestamp now, bool substitution) {
    const ClockReading reading = readClock(clock, now);
    if (reading.phase != MatchPhase::Break || substitution) {
        return reading.eventTime;
    }
    const int length = clock.format.periodLength;
    const PeriodTimes& times = clock.periods.back();
    const int added = addedPlayed(times, length);
    if (added > 0) {
        return {reading.period * length, added};
    }
    const int full = std::min(fullMinutes(elapsedSince(times.start, *times.end)), length - 1);
    return {(reading.period - 1) * length + full, std::nullopt};
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
    // At the end of a part its added time comes first; the same minute without added time is the
    // break or the start of the next part.
    std::ranges::stable_sort(order, {}, [&](std::size_t index) {
        const MatchEventDraft& event = events[index];
        return std::tuple{event.minute, event.addedTime ? 0 : 1, event.addedTime.value_or(0)};
    });
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
    for (std::size_t position = 0; position < order.size(); ++position) {
        const std::size_t index = order[position];
        const MatchEventDraft& event = events[index];
        if (finalMinute && event.minute > *finalMinute) {
            return std::unexpected(MatchEventError{MatchEventProblem::AfterFinalMinute, index});
        }
        if (auto problem = applyEvent(situation, event)) {
            return std::unexpected(MatchEventError{*problem, index});
        }
        // A second yellow card is followed by its red row: the next event of the player, at the
        // same time.
        if (event.type == MatchEventType::Yellow &&
            situation.find(*event.playerId)->yellowCards == 2) {
            const auto next =
                std::ranges::find_if(order.begin() + static_cast<std::ptrdiff_t>(position) + 1,
                                     order.end(), [&](std::size_t other) {
                                         return events[other].playerId == event.playerId ||
                                                events[other].relatedPlayerId == event.playerId;
                                     });
            const bool redFollows =
                next != order.end() && events[*next].type == MatchEventType::Red &&
                events[*next].playerId == event.playerId && !events[*next].relatedPlayerId &&
                events[*next].minute == event.minute && events[*next].addedTime == event.addedTime;
            if (!redFollows) {
                return std::unexpected(
                    MatchEventError{MatchEventProblem::SecondYellowWithoutRed, index});
            }
        }
    }
    return situation;
}

int onPitchCount(const MatchSituation& situation) noexcept {
    return static_cast<int>(
        std::ranges::count(situation.players, PlayerMatchState::OnPitch, &PlayerInMatch::state));
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
