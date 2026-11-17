#pragma once

#include "core/algorithms/MatchMinutes.hpp"
#include "core/domain/Enums.hpp"
#include "core/domain/Ids.hpp"
#include "core/domain/MatchEvent.hpp"
#include "core/domain/MatchNote.hpp"
#include "core/domain/Player.hpp"
#include "core/util/Error.hpp"
#include "services/CalendarService.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace fdss::data {
class ClubRepository;
class DatabaseManager;
class LineupRepository;
class MatchLiveRepository;
class MatchNoteRepository;
class MatchRepository;
class OpponentRepository;
class PlayerRepository;
} // namespace fdss::data

namespace fdss::services {

class Clock;
class LineupService;
class Session;

// One action of Match Day: what undo takes back, or would take back.
struct MatchDayAction {
    enum class Kind { Event, SecondYellow, Note, ClockStep, Finish };
    Kind kind = Kind::Event;
    // Event; for SecondYellow the yellow card (its red row goes with it).
    std::optional<core::MatchEvent> event;
    std::optional<core::MatchNote> note;
    // ClockStep; for Finish the end of the part written with it, if one was.
    std::optional<core::ClockStep> step;
};

// What Match Day shows for one match.
struct MatchDayState {
    CalendarMatch match;
    core::TeamCategory category = core::TeamCategory::Senior;
    // Empty until Match Day starts (a planned match).
    std::optional<core::MatchClock> clock;
    // From the saved lineup; empty without one.
    core::MatchSheet sheet;
    bool hasLineup = false;
    // The players of the sheet, also archived ones.
    std::vector<core::Player> players;
    // In the order of the match (core::replayOrder()).
    std::vector<core::MatchEvent> events;
    std::vector<core::MatchNote> notes;
    core::MatchSituation situation;
    // What undo would take back; nullopt: nothing.
    std::optional<MatchDayAction> lastAction;
};

struct MatchDayStartCheck {
    enum class Block { None, NotPlanned, AnotherLive, NoLineup, LineupErrors };
    Block block = Block::None;
    // The validator errors of the saved lineup, with players no longer in the squad.
    int lineupErrors = 0;
    core::MatchFormat defaultFormat;
};

// Why an event (entered late, at its minute) cannot be recorded.
struct EventConflict {
    core::MatchEventProblem problem = core::MatchEventProblem::UnknownPlayer;
    // A stored event that would no longer fit (e.g. a goal by a player now sent off before it);
    // empty when the new event itself does not fit.
    std::optional<core::MatchEvent> laterEvent;
};

// Match Day (SPEC section 4 and 6, packet P13). Every action is one transaction that writes the
// event, the note or the clock step with the next `seq` of the match (a counter that only grows, so
// no seq is used twice) and recomputes the appearances
// (core::computeMatchMinutes) from the lineup and the events, the single source of truth. Undo
// takes back the action entered last (the highest seq), whatever its minute. Reading needs a
// logged-in user of any role; the actions need RunMatchDay, otherwise PermissionDenied.
//
// Times: an action takes a core::MatchTime, or nullopt for core::defaultTime() now. It must be
// already played (core::isPlayedTime()); an event entered late goes through a full replay.
//
// Errors of the actions (field in brackets):
// - Conflict: a match that is not planned for start [match_not_planned]; another live match of the
//   team [another_live_match]; no saved lineup [no_lineup]; validator errors [lineup_errors]; a
//   match that is not live [match_not_live]; before the kick-off [match_not_running]; going back to
//   planned after the kick-off [match_started]; fewer than 7 players on the pitch: only the end of
//   the match, notes and undo [too_few_players]; an event entered late that a later stored event
//   no longer fits [event_conflict, see checkEvent()]; undoing the end of the match while another
//   match of the team is live [undo_end_another_live]; ending a match with 7 players or more
//   before its last part [match_not_over].
// - InvalidInput: a format outside 1–4 × 10–60, at most 130 minutes [match_format]; a minute not
//   played yet [minute_not_played]; an event that does not fit the match [player_unknown,
//   player_missing, same_player, player_not_on_pitch, player_not_on_bench, player_substituted,
//   player_sent_off, minute_out_of_range, after_final_minute, second_yellow_without_red]; a note of
//   1–500 characters [note_text].
// - NotFound: nothing to undo [nothing_to_undo].
// - Database: stored events of a started match that do not fit its lineup [match_history]; also
// from
//   state(), which never shows such a match as its lineup at 0:0. Matches that Match Day did not
//   start (no match_live row) show the lineup and the stored score instead.
class MatchDayService {
public:
    MatchDayService(data::DatabaseManager& db, data::MatchRepository& matches,
                    data::MatchLiveRepository& live, data::MatchNoteRepository& notes,
                    data::LineupRepository& lineups, data::ClubRepository& clubs,
                    data::OpponentRepository& opponents, data::PlayerRepository& players,
                    LineupService& lineupService, Session& session, const Clock& clock);

    // The live match of the team; nullopt: none.
    [[nodiscard]] core::Result<std::optional<core::MatchId>> liveMatch(core::TeamId team) const;
    [[nodiscard]] core::Result<MatchDayState> state(core::MatchId match) const;
    [[nodiscard]] core::Result<MatchDayStartCheck> startCheck(core::MatchId match,
                                                              int yellowCardThreshold) const;
    // What recording the event (with its time) would run into in a full replay, without writing
    // anything; nullopt when it fits. A second yellow card is checked with its red row. The time
    // itself is checked by the record methods.
    [[nodiscard]] core::Result<std::optional<EventConflict>>
    checkEvent(core::MatchId match, const core::MatchEventDraft& event) const;

    // Planned → live with the clock at 00:00, before the kick-off.
    [[nodiscard]] core::Status start(core::MatchId match, core::MatchFormat format,
                                     int yellowCardThreshold);
    // Live → planned while the first part has not started; the clock goes.
    [[nodiscard]] core::Status cancelStart(core::MatchId match);
    // The next step of the clock; the end of the last part finishes the match (endNow()).
    [[nodiscard]] core::Status advanceClock(core::MatchId match);
    // Finishes the match now (endNow()): with fewer than 7 players at any time, otherwise only
    // while the last part runs (Conflict [match_not_over]).
    [[nodiscard]] core::Status endMatch(core::MatchId match);
    [[nodiscard]] core::Status recordGoal(core::MatchId match, core::PlayerId scorer,
                                          std::optional<core::PlayerId> assistant,
                                          std::optional<core::MatchTime> time = std::nullopt);
    [[nodiscard]] core::Status
    recordOpponentGoal(core::MatchId match, std::optional<core::MatchTime> time = std::nullopt);
    // A second yellow card (counted at its time) also writes a red one, in the same action.
    [[nodiscard]] core::Status recordYellow(core::MatchId match, core::PlayerId player,
                                            std::optional<core::MatchTime> time = std::nullopt);
    [[nodiscard]] core::Status recordRed(core::MatchId match, core::PlayerId player,
                                         std::optional<core::MatchTime> time = std::nullopt);
    [[nodiscard]] core::Status
    recordSubstitution(core::MatchId match, core::PlayerId comingOn, core::PlayerId goingOff,
                       std::optional<core::MatchTime> time = std::nullopt);
    // Notes never change the score or the minutes, and are allowed with fewer than 7 players.
    // Adding one is an action of the undo stack; editing keeps its place there.
    [[nodiscard]] core::Result<core::MatchNoteId> addNote(core::MatchId match,
                                                          std::optional<core::MatchTime> time,
                                                          std::optional<core::PlayerId> player,
                                                          std::string_view text);
    [[nodiscard]] core::Status updateNote(core::MatchId match, core::MatchNoteId note,
                                          core::MatchTime time,
                                          std::optional<core::PlayerId> player,
                                          std::string_view text);
    // For the undo toast of the view: restoreNote() brings it back with its place in the stack.
    [[nodiscard]] core::Result<core::MatchNote> deleteNote(core::MatchId match,
                                                           core::MatchNoteId note);
    [[nodiscard]] core::Result<core::MatchNoteId> restoreNote(const core::MatchNote& note);
    // Takes back the action entered last. Undoing the finish makes the match live again and clears
    // the score.
    [[nodiscard]] core::Result<MatchDayAction> undoLast(core::MatchId match);

private:
    struct Live;

    [[nodiscard]] core::Status requireLogin() const;
    // Other when the team is not found.
    [[nodiscard]] core::Result<core::TeamCategory> categoryOf(core::TeamId team) const;
    // The match with its clock; Conflict [match_not_live] unless it is live (or, with
    // `allowFinished`, finished).
    [[nodiscard]] core::Result<Live> readLive(core::MatchId match, bool allowFinished) const;
    [[nodiscard]] core::Result<core::MatchSheet> sheetOf(core::MatchId match) const;
    // The rows of one event action (a second yellow adds its red) after a full replay with the
    // stored events. `conflict` tells why the replay failed.
    [[nodiscard]] core::Result<std::vector<core::MatchEventDraft>>
    prepareEvents(const Live& live, const core::MatchEventDraft& event,
                  std::optional<EventConflict>& conflict) const;
    [[nodiscard]] core::Status recordEvent(core::MatchId match, core::MatchEventDraft event,
                                           std::optional<core::MatchTime> time);
    // A part is running or in a break, and the time was played.
    [[nodiscard]] core::Result<core::MatchTime>
    timeFor(const Live& live, std::optional<core::MatchTime> time, bool substitution) const;
    [[nodiscard]] core::Status checkNote(const Live& live, std::optional<core::PlayerId> player,
                                         std::string_view text) const;
    // Within the transaction of the caller. Keeps the ratings and RPE already stored.
    [[nodiscard]] core::Result<core::MatchMinutes>
    recompute(core::MatchId match, const core::MatchSheet& sheet, std::optional<int> finalMinute);
    // The one way a match ends (P13 re-review): ends the part running, if one is; with fewer than
    // 7 players on the pitch it ends early at core::earlyEndMinute(), otherwise normally (parts ×
    // length). Writes the step, finished_seq, ended_early_minute, the minutes, status and score.
    [[nodiscard]] core::Status endNow(const Live& live, int seq);

    data::DatabaseManager& m_db;
    data::MatchRepository& m_matches;
    data::MatchLiveRepository& m_live;
    data::MatchNoteRepository& m_notes;
    data::LineupRepository& m_lineups;
    data::ClubRepository& m_clubs;
    data::OpponentRepository& m_opponents;
    data::PlayerRepository& m_players;
    LineupService& m_lineupService;
    Session& m_session;
    const Clock& m_clock;
};

} // namespace fdss::services
