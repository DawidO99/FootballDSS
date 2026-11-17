#pragma once

#include "core/domain/Appearance.hpp"
#include "core/domain/DateTypes.hpp"
#include "core/domain/Enums.hpp"
#include "core/domain/Ids.hpp"
#include "core/domain/Lineup.hpp"
#include "core/domain/MatchEvent.hpp"

#include <chrono>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <vector>

// Match Day (SPEC section 6 `match_event`, packet P13): the clock of a live match and the minutes
// of every player, recomputed from the lineup and the events, which are the single source of truth.
//
// Minutes (P13 decision 3 and 4): an event stores the full minutes elapsed in the match, 0-based,
// cut at the end of its half (the first half at most halfLength, the second at most 2 ×
// halfLength); the minutes over the end of the half go to `addedTime` for display only. A whole
// match is 2 × halfLength minutes, whatever the stoppage time.

namespace fdss::core {

inline constexpr int kMinHalfLength = 10;
inline constexpr int kMaxHalfLength = 60;
inline constexpr int kMaxMatchMinute = 130;

// SPEC: senior 45, U19 45, U17 40, U15 35, U13 30, U11 25, other 45.
[[nodiscard]] int defaultHalfLength(TeamCategory category) noexcept;
[[nodiscard]] constexpr bool isValidHalfLength(int minutes) noexcept {
    return minutes >= kMinHalfLength && minutes <= kMaxHalfLength;
}

enum class MatchPhase { NotStarted, FirstHalf, HalfTime, SecondHalf, Finished };

// The persisted clock: when each step happened (UTC).
struct MatchClock {
    int halfLength = 45;
    std::optional<Timestamp> firstHalfStart;
    std::optional<Timestamp> firstHalfEnd;
    std::optional<Timestamp> secondHalfStart;
    std::optional<Timestamp> matchEnd;

    friend bool operator==(const MatchClock&, const MatchClock&) = default;
};

[[nodiscard]] MatchPhase phaseOf(const MatchClock& clock) noexcept;
// The step the clock button takes next; nullopt once the match is finished.
[[nodiscard]] std::optional<ClockStep> nextStep(const MatchClock& clock) noexcept;
// The last step taken; nullopt before the first half.
[[nodiscard]] std::optional<ClockStep> lastStep(const MatchClock& clock) noexcept;
// Sets (or with nullopt clears) the time of a step.
void setStep(MatchClock& clock, ClockStep step, std::optional<Timestamp> at) noexcept;

// What an event recorded at a moment stores.
struct MatchTime {
    int minute = 0;
    std::optional<int> addedTime;

    friend bool operator==(const MatchTime&, const MatchTime&) = default;
};

// The football way of showing a minute: 0 → 1', 44 → 45', {45, +3} → 45+3'.
struct DisplayMinute {
    int minute = 1;
    std::optional<int> added;

    friend bool operator==(const DisplayMinute&, const DisplayMinute&) = default;
};
[[nodiscard]] DisplayMinute displayMinute(const MatchTime& time) noexcept;

struct ClockReading {
    MatchPhase phase = MatchPhase::NotStarted;
    // The regular part of the clock: the match time up to the end of the current half.
    std::chrono::seconds regular{0};
    // Time over the end of the half; 0 outside stoppage time. The clock shows "45+2:10".
    std::chrono::seconds stoppage{0};
    // What an event recorded now stores. In the half-time break: the end of the first half.
    MatchTime eventTime;
};
// A time before the start of a half (the system clock went back) counts as 0.
[[nodiscard]] ClockReading readClock(const MatchClock& clock, Timestamp now) noexcept;

// Who starts and who is on the bench, from the saved lineup.
struct StartingPlayer {
    PlayerId player;
    Position position = Position::Cm;

    friend bool operator==(const StartingPlayer&, const StartingPlayer&) = default;
};

struct MatchSheet {
    std::vector<StartingPlayer> starters; // the filled slots, in slot order
    std::vector<PlayerId> bench;

    friend bool operator==(const MatchSheet&, const MatchSheet&) = default;
};

[[nodiscard]] MatchSheet matchSheetOf(const Lineup& lineup);

enum class PlayerMatchState { Bench, OnPitch, SubstitutedOff, SentOff };

struct PlayerInMatch {
    PlayerId player;
    PlayerMatchState state = PlayerMatchState::Bench;
    bool started = false;
    std::optional<int> minuteOn;
    std::optional<int> minuteOff;
    std::optional<Position> position;
    int yellowCards = 0;
    int goals = 0;
    int assists = 0;

    friend bool operator==(const PlayerInMatch&, const PlayerInMatch&) = default;
};

// The state of the match after a list of events.
struct MatchSituation {
    // The starters in slot order, then the bench in order.
    std::vector<PlayerInMatch> players;
    int goalsFor = 0;
    int goalsAgainst = 0;

    [[nodiscard]] const PlayerInMatch* find(PlayerId player) const noexcept;
    [[nodiscard]] PlayerInMatch* find(PlayerId player) noexcept;
};

[[nodiscard]] MatchSituation startingSituation(const MatchSheet& sheet);

// Why an event does not fit the match.
enum class MatchEventProblem {
    UnknownPlayer,      // not in the lineup of the match
    MissingPlayer,      // a player is required (and both for a substitution)
    SamePlayer,         // scorer = assistant, or in = out
    NotOnPitch,         // the player (out, scorer, assistant, card) is not on the pitch
    NotOnBench,         // the player coming on is already on the pitch
    AlreadySubstituted, // the player has left by a substitution and cannot come back
    SentOff,            // a sent-off player cannot be substituted or come on
    MinuteOutOfRange,   // outside 0–130, or added time below 1
    AfterFinalMinute    // after the end of the match
};

struct MatchEventError {
    MatchEventProblem problem = MatchEventProblem::UnknownPlayer;
    // The position of the event in the list given.
    std::size_t index = 0;

    friend bool operator==(const MatchEventError&, const MatchEventError&) = default;
};

// Nothing changes when the event does not fit.
[[nodiscard]] std::optional<MatchEventProblem> applyEvent(MatchSituation& situation,
                                                          const MatchEventDraft& event);
[[nodiscard]] std::optional<MatchEventProblem> checkEvent(const MatchSituation& situation,
                                                          const MatchEventDraft& event);

// Applies the events by minute, keeping the given order within one minute (the repository lists
// them by minute, then as recorded). finalMinute: nullopt while the match runs; otherwise events
// after it are rejected.
[[nodiscard]] std::expected<MatchSituation, MatchEventError>
replayMatch(const MatchSheet& sheet, std::span<const MatchEventDraft> events,
            std::optional<int> finalMinute = std::nullopt);

// The order in which replayMatch() applies the events: by minute, then as given.
[[nodiscard]] std::vector<std::size_t> replayOrder(std::span<const MatchEventDraft> events);

// For each event: true for the red card of a second yellow, i.e. a `red` row without a related
// player whose player already had two yellow cards, counted in replay order (P13 review). A yellow
// and a straight red at the same minute is a red card.
[[nodiscard]] std::vector<bool> secondYellowReds(std::span<const MatchEventDraft> events);

struct MatchMinutes {
    // The starters and every player who came on, in the order of the situation. Starters from 0,
    // substitutes from their minute; minuteOff is the substitution, the red card or the final
    // minute, and empty for a player still on the pitch of a running match. Without rating or RPE.
    std::vector<AppearanceDraft> appearances;
    int goalsFor = 0;
    int goalsAgainst = 0;

    friend bool operator==(const MatchMinutes&, const MatchMinutes&) = default;
};

[[nodiscard]] MatchMinutes minutesOf(const MatchSituation& situation,
                                     std::optional<int> finalMinute);

[[nodiscard]] std::expected<MatchMinutes, MatchEventError>
computeMatchMinutes(const MatchSheet& sheet, std::span<const MatchEventDraft> events,
                    std::optional<int> finalMinute);

} // namespace fdss::core
