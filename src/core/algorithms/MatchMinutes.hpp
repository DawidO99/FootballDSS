#pragma once

#include "core/domain/Appearance.hpp"
#include "core/domain/DateTypes.hpp"
#include "core/domain/Enums.hpp"
#include "core/domain/Ids.hpp"
#include "core/domain/Lineup.hpp"
#include "core/domain/MatchEvent.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// Match Day (SPEC section 6 `match_event`, packet P13): the clock of a live match and the minutes
// of every player, recomputed from the lineup and the events, which are the single source of truth.
//
// Format: 1–4 parts of 10–60 minutes (2 parts are the halves). Minutes (P13 decisions): an event
// stores the full minutes elapsed in the match, 0-based, cut at the end of its part (part p ends at
// p × length); the minutes over it go to `addedTime` for display only. A whole match is
// parts × length minutes, whatever the stoppage time.

namespace fdss::core {

inline constexpr int kMinPeriods = 1;
inline constexpr int kMaxPeriods = 4;
inline constexpr int kMinPeriodLength = 10;
inline constexpr int kMaxPeriodLength = 60;
// The CHECK of `match_event.minute` and of the appearance minutes (migration 0001).
inline constexpr int kMaxMatchMinute = 130;
// By the laws of the game a team needs at least 7 players on the pitch to go on.
inline constexpr int kMinPlayersOnPitch = 7;

struct MatchFormat {
    int periods = 2;
    int periodLength = 45;

    [[nodiscard]] constexpr int totalMinutes() const noexcept { return periods * periodLength; }

    friend bool operator==(const MatchFormat&, const MatchFormat&) = default;
};

// 1–4 parts of 10–60 minutes, at most kMaxMatchMinute in all.
[[nodiscard]] constexpr bool isValidMatchFormat(MatchFormat format) noexcept {
    return format.periods >= kMinPeriods && format.periods <= kMaxPeriods &&
           format.periodLength >= kMinPeriodLength && format.periodLength <= kMaxPeriodLength &&
           format.totalMinutes() <= kMaxMatchMinute;
}

// The variants offered at the start (packet P13): 2×45, 2×40, 2×35, 2×30, 2×25, 3×30, 4×20.
inline constexpr std::array<MatchFormat, 7> kMatchFormatPresets{
    {{2, 45}, {2, 40}, {2, 35}, {2, 30}, {2, 25}, {3, 30}, {4, 20}}};

// By team category: senior 2×45, U19 2×45, U17 2×40, U15 2×35, U13 2×30, U11 2×25, other 2×45.
[[nodiscard]] MatchFormat defaultMatchFormat(TeamCategory category) noexcept;

// NotStarted: before the first part; Running: a part is played; Break: between two parts;
// Finished: after the last part, or ended early.
enum class MatchPhase { NotStarted, Running, Break, Finished };

struct PeriodTimes {
    Timestamp start{};
    std::optional<Timestamp> end;

    friend bool operator==(const PeriodTimes&, const PeriodTimes&) = default;
};

// The persisted clock: the parts started so far, in order (UTC).
struct MatchClock {
    MatchFormat format;
    std::vector<PeriodTimes> periods;
    // The match was finished: after the last part, or early (fewer than 7 players).
    bool finished = false;
    // Finished early: the final minute is the minute of the end, not parts × length.
    bool endedEarly = false;

    friend bool operator==(const MatchClock&, const MatchClock&) = default;
};

struct ClockStep {
    int period = 1; // 1-based
    ClockStepKind kind = ClockStepKind::Start;

    friend bool operator==(const ClockStep&, const ClockStep&) = default;
};

[[nodiscard]] MatchPhase phaseOf(const MatchClock& clock) noexcept;
// The part being played, or the last one played; 0 before the first.
[[nodiscard]] int currentPeriod(const MatchClock& clock) noexcept;
// The step the clock button takes next; nullopt once the match is finished. The end of the last
// part ends the match.
[[nodiscard]] std::optional<ClockStep> nextStep(const MatchClock& clock) noexcept;
// Records a step; a step out of order is ignored.
void applyStep(MatchClock& clock, ClockStep step, Timestamp at);
// The minute the match ended: parts × length (whatever the stoppage time, or a last part ended a
// little before its length), or for a match ended early the minute its last part ended, at most the
// end of that part.
[[nodiscard]] int finalMinute(const MatchClock& clock) noexcept;

// What an event stores.
struct MatchTime {
    int minute = 0;
    std::optional<int> addedTime;

    friend bool operator==(const MatchTime&, const MatchTime&) = default;
};

// The football way of showing a minute: 0 → 1', 44 → 45', {45, +3} → 45+3', {30, +2} → 30+2'.
struct DisplayMinute {
    int minute = 1;
    std::optional<int> added;

    friend bool operator==(const DisplayMinute&, const DisplayMinute&) = default;
};
[[nodiscard]] DisplayMinute displayMinute(const MatchTime& time) noexcept;
// "23", "23'", "45+2", "45+2'" (spaces allowed around); nullopt for anything else.
[[nodiscard]] std::optional<DisplayMinute> parseDisplayMinute(std::string_view text);
// The stored time of a shown minute; added time only at the end of a part ("30+2" in 3×30).
[[nodiscard]] std::optional<MatchTime> matchTimeOf(DisplayMinute shown, MatchFormat format);

struct ClockReading {
    MatchPhase phase = MatchPhase::NotStarted;
    int period = 0;
    // The regular part of the clock: the match time up to the end of the current part.
    std::chrono::seconds regular{0};
    // Time over the end of the part; 0 outside stoppage time. The clock shows "45+2:10".
    std::chrono::seconds stoppage{0};
    // What an event recorded now stores. In a break: the end of the part played (the minute of a
    // substitution in the break); finished: the final minute.
    MatchTime eventTime;
};
// A time before the start of a part (the system clock went back) counts as 0.
[[nodiscard]] ClockReading readClock(const MatchClock& clock, Timestamp now) noexcept;

// Whether a time was already played: a minute of a part started so far, not later than now, or the
// added time of a part as far as it was played. A substitution in a break may also take the minute
// that starts the next part ("46'" in the half-time break).
[[nodiscard]] bool isPlayedTime(const MatchClock& clock, Timestamp now, MatchTime time,
                                bool substitution);
// What the minute field offers: the time now while a part runs; in a break the minute of a
// substitution, or for anything else the last minute played.
[[nodiscard]] MatchTime defaultTime(const MatchClock& clock, Timestamp now, bool substitution);

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
    AfterFinalMinute,   // after the end of the match
    // A second yellow card not followed by its red row at the same time: a yellow entered later
    // made a later card the second one (P13 late entry).
    SecondYellowWithoutRed
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

// Applies the events in replayOrder(). finalMinute: nullopt while the match runs; otherwise events
// after it are rejected.
[[nodiscard]] std::expected<MatchSituation, MatchEventError>
replayMatch(const MatchSheet& sheet, std::span<const MatchEventDraft> events,
            std::optional<int> finalMinute = std::nullopt);

// The order of the match: by minute; at one minute the added time of the part that ends there (by
// added minutes) comes before the minute that starts the next part; then as given (the repository
// lists them as recorded).
[[nodiscard]] std::vector<std::size_t> replayOrder(std::span<const MatchEventDraft> events);

// For each event: true for the red card of a second yellow, i.e. a `red` row without a related
// player whose player already had two yellow cards, counted in replay order (P13 review). A yellow
// and a straight red at the same minute is a red card.
[[nodiscard]] std::vector<bool> secondYellowReds(std::span<const MatchEventDraft> events);

[[nodiscard]] int onPitchCount(const MatchSituation& situation) noexcept;

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
