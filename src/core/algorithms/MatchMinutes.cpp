#include "core/algorithms/MatchMinutes.hpp"

#include "core/algorithms/Formations.hpp"

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
    if (clock.clockless) {
        return MatchPhase::Running;
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
    if (clock.clockless) {
        return std::nullopt;
    }
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
    if (currentPeriod(clock) == 0 && !clock.clockless) {
        return 0;
    }
    return clock.endedEarlyAt.value_or(clock.format.totalMinutes());
}

std::optional<MatchTime> latestRecordedTime(std::span<const MatchEventDraft> events,
                                            std::span<const MatchNoteDraft> notes,
                                            std::span<const MatchTacticChangeDraft> changes) {
    std::optional<MatchTime> latest;
    // The order of the match: at the end of a part its added time before the next minute.
    const auto key = [](const MatchTime& time) {
        return std::tuple{time.minute, time.addedTime ? 0 : 1, time.addedTime.value_or(0)};
    };
    const auto consider = [&](int minute, std::optional<int> added) {
        const MatchTime time{minute, added};
        if (!latest || key(time) > key(*latest)) {
            latest = time;
        }
    };
    for (const MatchEventDraft& event : events) {
        consider(event.minute, event.addedTime);
    }
    for (const MatchNoteDraft& note : notes) {
        consider(note.minute, note.addedTime);
    }
    for (const MatchTacticChangeDraft& change : changes) {
        consider(change.minute, change.addedTime);
    }
    return latest;
}

int earlyEndMinute(const MatchClock& clock, Timestamp now,
                   std::optional<MatchTime> latest) noexcept {
    const int length = clock.format.periodLength;
    const int played = currentPeriod(clock);
    int minute = 0;
    if (played > 0 && !clock.clockless) {
        const PeriodTimes& last = clock.periods.back();
        // In a break: the end of the part played, the minute of a substitution in it.
        minute = last.end ? played * length
                          : (played - 1) * length +
                                std::min(fullMinutes(elapsedSince(last.start, now)), length);
    }
    if (latest) {
        minute = std::max(minute, latest->minute);
    }
    return std::min(minute, kMaxMatchMinute);
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
    if (clock.clockless) {
        // No stopwatch: nothing runs; a finished match stands at its final minute.
        if (reading.phase == MatchPhase::Finished) {
            reading.regular = finalMinute(clock) * kMinute;
            reading.eventTime = {finalMinute(clock), std::nullopt};
        }
        return reading;
    }
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

bool isValidMatchTime(MatchFormat format, MatchTime time) noexcept {
    const int length = format.periodLength;
    if (time.addedTime) {
        const int period = time.minute / length;
        return time.minute % length == 0 && period >= 1 && period <= format.periods &&
               *time.addedTime >= 1 && *time.addedTime <= kMaxAddedTime;
    }
    return time.minute >= 0 && time.minute < format.totalMinutes();
}

bool isPlayedTime(const MatchClock& clock, Timestamp now, MatchTime time, bool substitution) {
    if (clock.clockless) {
        return isValidMatchTime(clock.format, time);
    }
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

std::optional<std::chrono::seconds> elapsedFromClockText(std::string_view text, MatchFormat format,
                                                         int period) {
    const auto trim = [](std::string_view value) {
        while (!value.empty() && value.front() == ' ') {
            value.remove_prefix(1);
        }
        while (!value.empty() && value.back() == ' ') {
            value.remove_suffix(1);
        }
        return value;
    };
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
    // "mm:ss" or "mm" as seconds.
    const auto clockTime = [&](std::string_view value) -> std::optional<seconds> {
        value = trim(value);
        const std::size_t colon = value.find(':');
        const auto minutes =
            number(colon == std::string_view::npos ? value : value.substr(0, colon));
        if (!minutes) {
            return std::nullopt;
        }
        int secs = 0;
        if (colon != std::string_view::npos) {
            const std::string_view rest = value.substr(colon + 1);
            const auto parsed = number(rest);
            if (!parsed || rest.size() != 2 || *parsed > 59) {
                return std::nullopt;
            }
            secs = *parsed;
        }
        return *minutes * kMinute + seconds{secs};
    };
    if (period < 1 || period > format.periods) {
        return std::nullopt;
    }
    const int length = format.periodLength;
    text = trim(text);
    const std::size_t plus = text.find('+');
    if (plus != std::string_view::npos) {
        // Stoppage time: the end of this part, then the time over it.
        const auto base = number(trim(text.substr(0, plus)));
        const auto over = clockTime(text.substr(plus + 1));
        if (!base || !over || *base != period * length) {
            return std::nullopt;
        }
        return length * kMinute + *over;
    }
    const auto value = clockTime(text);
    const seconds start = (period - 1) * length * kMinute;
    if (!value || *value < start || *value > period * length * kMinute) {
        return std::nullopt;
    }
    return *value - start;
}

std::chrono::seconds minElapsedFor(MatchFormat format, int period,
                                   std::optional<MatchTime> latest) {
    const int length = format.periodLength;
    int minutes = 0;
    if (latest) {
        if (latest->addedTime && latest->minute == period * length) {
            minutes = length + *latest->addedTime - 1;
        } else if (!latest->addedTime && latest->minute >= (period - 1) * length &&
                   latest->minute < period * length) {
            minutes = latest->minute - (period - 1) * length;
        }
    }
    return minutes * kMinute;
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

std::expected<MatchSituation, MatchEventError>
situationAt(const MatchSheet& sheet, std::span<const MatchEventDraft> events, MatchTime time) {
    const auto key = [](int minute, std::optional<int> added) {
        return std::tuple{minute, added ? 0 : 1, added.value_or(0)};
    };
    const auto limit = key(time.minute, time.addedTime);
    std::vector<MatchEventDraft> before;
    for (const MatchEventDraft& event : events) {
        if (key(event.minute, event.addedTime) <= limit) {
            before.push_back(event);
        }
    }
    return replayMatch(sheet, before);
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

namespace {

// The order of the match of a time: at the end of a part its added time comes first.
std::tuple<int, int, int> timeKey(int minute, std::optional<int> added) {
    return std::tuple{minute, added ? 0 : 1, added.value_or(0)};
}

// The players of the slots after the events taken in `order` (indices into `events`): a substitute
// in the slot of the player going off, a player sent off leaves his slot empty.
Lineup occupancyAfter(const Lineup& lineup, std::span<const MatchEventDraft> events,
                      std::span<const std::size_t> order) {
    Lineup shape = lineup;
    shape.bench.clear();
    const auto slotOf = [&shape](PlayerId player) -> LineupSlot* {
        const auto found = std::ranges::find(shape.pitchSlots, std::optional<PlayerId>(player),
                                             &LineupSlot::player);
        return found == shape.pitchSlots.end() ? nullptr : &*found;
    };
    for (const std::size_t index : order) {
        const MatchEventDraft& event = events[index];
        if (!event.playerId) {
            continue;
        }
        if (event.type == MatchEventType::Sub && event.relatedPlayerId) {
            if (LineupSlot* slot = slotOf(*event.relatedPlayerId)) {
                slot->player = event.playerId;
            }
        } else if (event.type == MatchEventType::Red) {
            if (LineupSlot* slot = slotOf(*event.playerId)) {
                slot->player.reset();
            }
        }
    }
    return shape;
}

// The players of the slots after the events up to `time` (all with nullopt).
std::expected<Lineup, MatchEventError> occupiedAt(const Lineup& lineup,
                                                  std::span<const MatchEventDraft> events,
                                                  std::optional<MatchTime> time) {
    std::vector<std::size_t> indices;
    std::vector<MatchEventDraft> before;
    for (std::size_t index = 0; index < events.size(); ++index) {
        const MatchEventDraft& event = events[index];
        if (!time ||
            timeKey(event.minute, event.addedTime) <= timeKey(time->minute, time->addedTime)) {
            indices.push_back(index);
            before.push_back(event);
        }
    }
    // The events must fit the lineup; an error names the event in `events`.
    if (auto replayed = replayMatch(matchSheetOf(lineup), before); !replayed) {
        MatchEventError error = replayed.error();
        error.index = indices[error.index];
        return std::unexpected(error);
    }
    return occupancyAfter(lineup, before, replayOrder(before));
}

// The index of the change in force at `time` (nullopt: all): the last one up to it in the order of
// the match, then as given.
std::optional<std::size_t> changeInForce(std::span<const MatchTacticChangeDraft> changes,
                                         std::optional<MatchTime> time,
                                         std::optional<std::size_t> before = std::nullopt) {
    std::optional<std::size_t> found;
    for (std::size_t index = 0; index < changes.size(); ++index) {
        const MatchTacticChangeDraft& change = changes[index];
        const auto key = timeKey(change.minute, change.addedTime);
        if (time && key > timeKey(time->minute, time->addedTime)) {
            continue;
        }
        if (before) {
            const MatchTacticChangeDraft& limit = changes[*before];
            const auto limitKey = timeKey(limit.minute, limit.addedTime);
            if (key > limitKey || (key == limitKey && index >= *before)) {
                continue;
            }
        }
        if (!found || key >= timeKey(changes[*found].minute, changes[*found].addedTime)) {
            found = index;
        }
    }
    return found;
}

// The places of the change (or of the lineup) on the occupied slots, laid out where a slot is
// empty.
Lineup withPlaces(Lineup shape, const MatchTacticChangeDraft* change) {
    if (change != nullptr) {
        const std::size_t count =
            std::min(shape.pitchSlots.size(), change->shape.pitchSlots.size());
        for (std::size_t i = 0; i < count; ++i) {
            const LineupSlot& place = change->shape.pitchSlots[i];
            shape.pitchSlots[i].position = place.position;
            shape.pitchSlots[i].x = place.x;
            shape.pitchSlots[i].y = place.y;
        }
    }
    std::vector<Position> zonesWithEmpty;
    for (const LineupSlot& slot : shape.pitchSlots) {
        if (!slot.player &&
            std::ranges::find(zonesWithEmpty, slot.position) == zonesWithEmpty.end()) {
            zonesWithEmpty.push_back(slot.position);
        }
    }
    for (const Position zone : zonesWithEmpty) {
        layoutZone(shape, zone, true);
    }
    shape.formation = liveFormationName(shape);
    return shape;
}

} // namespace

std::expected<Lineup, MatchEventError> shapeAt(const Lineup& lineup,
                                               std::span<const MatchEventDraft> events,
                                               std::span<const MatchTacticChangeDraft> changes,
                                               std::optional<MatchTime> time) {
    auto occupied = occupiedAt(lineup, events, time);
    if (!occupied) {
        return std::unexpected(occupied.error());
    }
    const std::optional<std::size_t> change = changeInForce(changes, time);
    return withPlaces(std::move(*occupied), change ? &changes[*change] : nullptr);
}

std::expected<std::vector<ShapeChangeNames>, MatchEventError>
shapeChangeNames(const Lineup& lineup, std::span<const MatchEventDraft> events,
                 std::span<const MatchTacticChangeDraft> changes) {
    std::vector<ShapeChangeNames> names;
    names.reserve(changes.size());
    for (std::size_t index = 0; index < changes.size(); ++index) {
        const MatchTime time{changes[index].minute, changes[index].addedTime};
        auto occupied = occupiedAt(lineup, events, time);
        if (!occupied) {
            return std::unexpected(occupied.error());
        }
        const std::optional<std::size_t> previous = changeInForce(changes, time, index);
        names.push_back({withPlaces(*occupied, previous ? &changes[*previous] : nullptr).formation,
                         withPlaces(*occupied, &changes[index]).formation});
    }
    return names;
}

std::expected<MatchMinutes, MatchEventError>
computeMatchMinutes(const Lineup& lineup, std::span<const MatchEventDraft> events,
                    std::span<const MatchTacticChangeDraft> changes,
                    std::optional<int> finalMinute) {
    auto minutes = computeMatchMinutes(matchSheetOf(lineup), events, finalMinute);
    if (!minutes || changes.empty()) {
        return minutes;
    }
    // The events were checked above: replay them in the order of the match.
    const std::vector<std::size_t> order = replayOrder(events);
    for (AppearanceDraft& appearance : minutes->appearances) {
        if (appearance.started) {
            continue;
        }
        const auto sub = std::ranges::find_if(order, [&](std::size_t index) {
            return events[index].type == MatchEventType::Sub &&
                   events[index].playerId == appearance.playerId;
        });
        if (sub == order.end()) {
            continue;
        }
        // The pitch right after he came on, before the later events of the same minute (a red
        // card or his own substitution would take him off it again; P13b review), with the
        // tactic changes up to that time.
        const MatchEventDraft& event = events[*sub];
        const std::span<const std::size_t> upToHim(
            order.data(), static_cast<std::size_t>(sub - order.begin()) + 1);
        const std::optional<std::size_t> change =
            changeInForce(changes, MatchTime{event.minute, event.addedTime});
        const Lineup shape = withPlaces(occupancyAfter(lineup, events, upToHim),
                                        change ? &changes[*change] : nullptr);
        const auto slot = std::ranges::find(
            shape.pitchSlots, std::optional<PlayerId>(appearance.playerId), &LineupSlot::player);
        if (slot != shape.pitchSlots.end()) {
            appearance.position = slot->position;
        }
    }
    return minutes;
}

} // namespace fdss::core
