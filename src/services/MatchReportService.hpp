#pragma once

#include "core/algorithms/MatchMinutes.hpp"
#include "core/domain/Appearance.hpp"
#include "core/domain/DateTypes.hpp"
#include "core/domain/Enums.hpp"
#include "core/domain/Ids.hpp"
#include "core/domain/Lineup.hpp"
#include "core/domain/MatchEvent.hpp"
#include "core/domain/MatchNote.hpp"
#include "core/domain/MatchTacticChange.hpp"
#include "core/domain/Player.hpp"
#include "core/util/Error.hpp"
#include "services/CalendarService.hpp"
#include "services/MatchRecompute.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fdss::data {
class DatabaseManager;
class LineupRepository;
class MatchLiveRepository;
class MatchNoteRepository;
class MatchRepository;
class MatchTacticRepository;
class OpponentRepository;
class PlayerRepository;
} // namespace fdss::data

namespace fdss::services {

class Session;

// A shape the team played, with its minutes: split only by the changes of shape (P14 decision 12).
struct ShapePeriod {
    // core::liveFormationName() at the start of the period; no translated text.
    std::string formation;
    core::MatchTime from;
    // nullopt: until the end of the match.
    std::optional<core::MatchTime> to;

    friend bool operator==(const ShapePeriod&, const ShapePeriod&) = default;
};

// What the match report shows (SPEC section 4, packet P14).
struct MatchReport {
    CalendarMatch match;
    std::optional<std::string> reportNote;
    // The format of Match Day; for a match without it, guessed from the final minute (2 × half of
    // it, if that is a format; otherwise one part), so a minute such as "45+2" can be read.
    core::MatchFormat format;
    // Match Day ran the match (a `match_live` row and a saved lineup).
    bool fromMatchDay = false;
    int finalMinute = 0;
    // The players who played, in the order of the sheet.
    std::vector<core::Appearance> appearances;
    // Every player of the sheet and of the appearances, also archived ones.
    std::vector<core::Player> players;
    // The sheet of the corrections: the saved lineup, or the one rebuilt from the appearances, with
    // the rest of the squad on the bench so a forgotten substitution can be added.
    core::MatchSheet sheet;
    // In the order of the match.
    std::vector<core::MatchEvent> events;
    std::vector<core::MatchNote> notes;
    std::vector<core::MatchTacticChange> tacticChanges;
    // For every change of shape: the names before and after it (core::shapeChangeNames()).
    std::vector<core::ShapeChangeNames> tacticChangeNames;
    std::vector<ShapePeriod> shapes;
    // Events can be added and deleted: the stored events replay on the sheet and give the stored
    // minutes and score (P14 decision 2). Otherwise ratings, RPE and notes only.
    bool correctable = false;
};

// One finished match of a player, for the "Matches" tab of the player card.
struct PlayerMatchRow {
    core::MatchId match;
    core::Date date{};
    std::string opponentName;
    core::Venue venue = core::Venue::Home;
    int goalsFor = 0;
    int goalsAgainst = 0;
    int minutes = 0;
    int goals = 0;
    int assists = 0;
    int yellowCards = 0;
    int redCards = 0;
    std::optional<int> rating;
    std::optional<int> rpe;

    friend bool operator==(const PlayerMatchRow&, const PlayerMatchRow&) = default;
};

// The rating (1–10) and RPE (0–10) of one player of the match; nullopt clears it.
struct RatingEntry {
    core::PlayerId player;
    std::optional<int> rating;
    std::optional<int> rpe;
};

// The match report (SPEC section 4 and 6, packet P14). Reading needs a logged-in user of any role;
// saving and the corrections need EditLineup, otherwise PermissionDenied, and a finished match.
// Every correction is one transaction: it writes or deletes the rows, replays the whole match on
// the sheet and recomputes the appearances (ratings and RPE stay) and the score. The final minute
// of the match never changes. Events and notes added here have no seq (they are not actions of
// Match Day); a row restored from the undo toast keeps its id and seq.
//
// Errors (field in brackets):
// - Conflict: the match is not finished [match_not_finished]; its events cannot be corrected
//   [not_correctable]; a later stored event would no longer fit the new one [event_conflict] or
//   the deleted one [event_dependent] (see checkEvent(), checkDeleteEvent()).
// - InvalidInput: a rating outside 1–10 [rating], an RPE outside 0–10 [rpe], or for a player who
// did
//   not play [player_unknown]; a report note over 2000 characters [report_note]; a note of 1–500
//   characters [note_text]; a minute outside the format [minute_out_of_match]; an event that does
//   not fit the match [player_unknown, player_missing, same_player, player_not_on_pitch,
//   player_not_on_bench, player_substituted, player_sent_off, after_final_minute…].
// - NotFound: a missing match, event, note or change of shape.
class MatchReportService {
public:
    static constexpr int kMaxReportNoteLength = 2000;

    MatchReportService(data::DatabaseManager& db, data::MatchRepository& matches,
                       data::MatchLiveRepository& live, data::MatchNoteRepository& notes,
                       data::MatchTacticRepository& tactics, data::LineupRepository& lineups,
                       data::OpponentRepository& opponents, data::PlayerRepository& players,
                       Session& session);

    [[nodiscard]] core::Result<MatchReport> report(core::MatchId match) const;
    // The finished matches of the player, the most recent first.
    [[nodiscard]] core::Result<std::vector<PlayerMatchRow>>
    playerMatches(core::PlayerId player) const;

    // Ratings, RPE and the report note in one transaction; a note empty after trimming is none.
    [[nodiscard]] core::Status save(core::MatchId match, std::span<const RatingEntry> ratings,
                                    std::string_view note);

    // The match at a time, for choosing the players of an event to add (core::situationAt()).
    [[nodiscard]] core::Result<core::MatchSituation> situationAt(core::MatchId match,
                                                                 core::MatchTime time) const;
    // What adding the event would run into, without writing anything; nullopt when it fits.
    [[nodiscard]] core::Result<std::optional<EventConflict>>
    checkEvent(core::MatchId match, const core::MatchEventDraft& event) const;
    // A second yellow (counted at its time) also writes its red row.
    [[nodiscard]] core::Result<std::vector<core::MatchEventId>>
    addEvent(core::MatchId match, const core::MatchEventDraft& event);
    // The later event that deleting this one would leave without its reason (a goal of a player
    // whose substitution goes, a second yellow whose first one goes); nullopt: none.
    [[nodiscard]] core::Result<std::optional<core::MatchEvent>>
    checkDeleteEvent(core::MatchId match, core::MatchEventId event) const;
    // Deletes the event (a second yellow and its red together) and returns the rows for undo.
    [[nodiscard]] core::Result<std::vector<core::MatchEvent>> deleteEvent(core::MatchId match,
                                                                          core::MatchEventId event);
    [[nodiscard]] core::Status restoreEvents(core::MatchId match,
                                             std::span<const core::MatchEvent> events);

    // Notes of the match with their minute; never change the minutes or the score.
    [[nodiscard]] core::Result<core::MatchNoteId> addNote(core::MatchId match, core::MatchTime time,
                                                          std::optional<core::PlayerId> player,
                                                          std::string_view text);
    [[nodiscard]] core::Status updateNote(core::MatchId match, core::MatchNoteId note,
                                          core::MatchTime time,
                                          std::optional<core::PlayerId> player,
                                          std::string_view text);
    [[nodiscard]] core::Result<core::MatchNote> deleteNote(core::MatchId match,
                                                           core::MatchNoteId note);
    [[nodiscard]] core::Result<core::MatchNoteId> restoreNote(const core::MatchNote& note);

    // Changes of shape are only deleted in the report (P14 decision 4); the appearances are
    // recomputed (a substitute takes the position of his slot).
    [[nodiscard]] core::Result<core::MatchTacticChange>
    deleteTacticChange(core::MatchId match, core::MatchTacticChangeId change);
    [[nodiscard]] core::Status restoreTacticChange(const core::MatchTacticChange& change);

private:
    struct Basis;

    [[nodiscard]] core::Status requireLogin() const;
    // Everything about the match the report and the corrections need.
    [[nodiscard]] core::Result<Basis> readBasis(core::MatchId match) const;
    // EditLineup, a finished match and, with `corrections`, a correctable one.
    [[nodiscard]] core::Result<Basis> readForChange(core::MatchId match, bool corrections) const;
    [[nodiscard]] core::Status checkTime(const Basis& basis, core::MatchTime time) const;
    [[nodiscard]] core::Status checkNote(const Basis& basis, std::optional<core::PlayerId> player,
                                         std::string_view text) const;
    // The rows deleting the event takes, and the later event that would lose its reason.
    [[nodiscard]] core::Result<std::vector<core::MatchEvent>>
    rowsToDelete(const Basis& basis, core::MatchEventId event,
                 std::optional<core::MatchEvent>& dependent) const;
    // Within the transaction of the caller: minutes and score from the stored events.
    [[nodiscard]] core::Status recompute(const Basis& basis);

    data::DatabaseManager& m_db;
    data::MatchRepository& m_matches;
    data::MatchLiveRepository& m_live;
    data::MatchNoteRepository& m_notes;
    data::MatchTacticRepository& m_tactics;
    data::LineupRepository& m_lineups;
    data::OpponentRepository& m_opponents;
    data::PlayerRepository& m_players;
    Session& m_session;
};

} // namespace fdss::services
