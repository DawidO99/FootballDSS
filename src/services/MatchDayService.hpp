#pragma once

#include "core/algorithms/MatchMinutes.hpp"
#include "core/domain/Enums.hpp"
#include "core/domain/Ids.hpp"
#include "core/domain/MatchEvent.hpp"
#include "core/domain/Player.hpp"
#include "core/util/Error.hpp"
#include "services/CalendarService.hpp"

#include <optional>
#include <vector>

namespace fdss::data {
class ClubRepository;
class DatabaseManager;
class LineupRepository;
class MatchLiveRepository;
class MatchRepository;
class OpponentRepository;
class PlayerRepository;
} // namespace fdss::data

namespace fdss::services {

class Clock;
class LineupService;
class Session;

// What undo takes back, or would take back.
struct MatchDayAction {
    enum class Kind { Event, SecondYellow, ClockStep };
    Kind kind = Kind::Event;
    // Event and SecondYellow (the yellow card of the two rows).
    std::optional<core::MatchEvent> event;
    // ClockStep.
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
    // By minute, then as recorded.
    std::vector<core::MatchEvent> events;
    core::MatchSituation situation;
    // What undo would take back; nullopt: nothing.
    std::optional<MatchDayAction> lastAction;
};

struct MatchDayStartCheck {
    enum class Block { None, NotPlanned, AnotherLive, NoLineup, LineupErrors };
    Block block = Block::None;
    // The validator errors of the saved lineup, with players no longer in the squad.
    int lineupErrors = 0;
    int defaultHalfLength = 45;
};

// Match Day (SPEC section 4 and 6, packet P13). Every action is one transaction that writes the
// event or the clock step and recomputes the appearances (core::computeMatchMinutes) from the
// lineup and the events, the single source of truth. Reading needs a logged-in user of any role;
// the actions need RunMatchDay, otherwise PermissionDenied.
//
// Errors of the actions (field in brackets):
// - Conflict: a match that is not planned for start [match_not_planned]; another live match of the
//   team [another_live_match]; no saved lineup [no_lineup]; validator errors [lineup_errors]; a
//   match that is not live [match_not_live]; no half running, or a card or goal in the half-time
//   break [match_not_running]; going back to planned after the kick-off [match_started].
// - InvalidInput: half length outside 10–60 [half_length]; an event that does not fit the match
//   [player_unknown, player_missing, same_player, player_not_on_pitch, player_not_on_bench,
//   player_substituted, player_sent_off, minute_out_of_range, after_final_minute].
// - NotFound: nothing to undo [nothing_to_undo].
class MatchDayService {
public:
    MatchDayService(data::DatabaseManager& db, data::MatchRepository& matches,
                    data::MatchLiveRepository& live, data::LineupRepository& lineups,
                    data::ClubRepository& clubs, data::OpponentRepository& opponents,
                    data::PlayerRepository& players, LineupService& lineupService, Session& session,
                    const Clock& clock);

    // The live match of the team; nullopt: none.
    [[nodiscard]] core::Result<std::optional<core::MatchId>> liveMatch(core::TeamId team) const;
    [[nodiscard]] core::Result<MatchDayState> state(core::MatchId match) const;
    [[nodiscard]] core::Result<MatchDayStartCheck> startCheck(core::MatchId match,
                                                              int yellowCardThreshold) const;

    // Planned → live with the clock at 00:00, before the kick-off.
    [[nodiscard]] core::Status start(core::MatchId match, int halfLength, int yellowCardThreshold);
    // Live → planned while the first half has not started; the clock goes.
    [[nodiscard]] core::Status cancelStart(core::MatchId match);
    // The next step of the clock; the end of the match sets finished and the score.
    [[nodiscard]] core::Status advanceClock(core::MatchId match);
    [[nodiscard]] core::Status recordGoal(core::MatchId match, core::PlayerId scorer,
                                          std::optional<core::PlayerId> assistant);
    [[nodiscard]] core::Status recordOpponentGoal(core::MatchId match);
    // A second yellow card also writes a red one (without a related player).
    [[nodiscard]] core::Status recordYellow(core::MatchId match, core::PlayerId player);
    [[nodiscard]] core::Status recordRed(core::MatchId match, core::PlayerId player);
    [[nodiscard]] core::Status recordSubstitution(core::MatchId match, core::PlayerId comingOn,
                                                  core::PlayerId goingOff);
    // Takes back the last event or clock step. Undoing the end of the match makes it live again
    // and clears the score.
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
    // Writes the events of one action at the current minute and recomputes.
    [[nodiscard]] core::Status recordEvents(core::MatchId match,
                                            std::vector<core::MatchEventDraft> events,
                                            bool allowedAtHalfTime);
    // Within the transaction of the caller. Keeps the ratings and RPE already stored.
    [[nodiscard]] core::Result<core::MatchMinutes>
    recompute(core::MatchId match, const core::MatchSheet& sheet, std::optional<int> finalMinute);

    data::DatabaseManager& m_db;
    data::MatchRepository& m_matches;
    data::MatchLiveRepository& m_live;
    data::LineupRepository& m_lineups;
    data::ClubRepository& m_clubs;
    data::OpponentRepository& m_opponents;
    data::PlayerRepository& m_players;
    LineupService& m_lineupService;
    Session& m_session;
    const Clock& m_clock;
};

} // namespace fdss::services
