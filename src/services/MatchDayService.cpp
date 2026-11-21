#include "services/MatchDayService.hpp"

#include "core/algorithms/Formations.hpp"
#include "core/algorithms/LineupValidator.hpp"
#include "core/domain/Validation.hpp"
#include "data/db/Transaction.hpp"
#include "data/repositories/ClubRepository.hpp"
#include "data/repositories/LineupRepository.hpp"
#include "data/repositories/MatchLiveRepository.hpp"
#include "data/repositories/MatchNoteRepository.hpp"
#include "data/repositories/MatchRepository.hpp"
#include "data/repositories/MatchTacticRepository.hpp"
#include "data/repositories/OpponentRepository.hpp"
#include "data/repositories/PlayerRepository.hpp"
#include "services/Clock.hpp"
#include "services/LineupService.hpp"
#include "services/Session.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace fdss::services {

namespace {

constexpr std::string_view kMatchNotPlanned = "match_not_planned";
constexpr std::string_view kAnotherLiveMatch = "another_live_match";
constexpr std::string_view kNoLineup = "no_lineup";
constexpr std::string_view kLineupErrors = "lineup_errors";
constexpr std::string_view kMatchNotLive = "match_not_live";
constexpr std::string_view kMatchNotRunning = "match_not_running";
constexpr std::string_view kMatchStarted = "match_started";
constexpr std::string_view kMatchFormat = "match_format";
constexpr std::string_view kMinuteNotPlayed = "minute_not_played";
constexpr std::string_view kTooFewPlayers = "too_few_players";
constexpr std::string_view kMatchNotOver = "match_not_over";
constexpr std::string_view kClockless = "clockless";
constexpr std::string_view kMinuteRequired = "minute_required";
constexpr std::string_view kMinuteOutOfMatch = "minute_out_of_match";
constexpr std::string_view kClockNotRunning = "clock_not_running";
constexpr std::string_view kClockTime = "clock_time";
constexpr std::string_view kClockBeforeEvent = "clock_before_event";
constexpr std::string_view kEventConflict = "event_conflict";
constexpr std::string_view kNoteText = "note_text";
constexpr std::string_view kNothingToUndo = "nothing_to_undo";
constexpr std::string_view kUndoEndAnotherLive = "undo_end_another_live";
constexpr std::string_view kMatchHistory = "match_history";
constexpr std::string_view kTacticShape = "tactic_shape";
constexpr std::string_view kTacticUnchanged = "tactic_unchanged";
constexpr std::string_view kTacticChangeLater = "tactic_change_later";

core::Error conflict(std::string_view field, std::string detail) {
    return core::Error{core::ErrorCode::Conflict, std::move(detail), std::string(field)};
}

core::Error invalid(std::string_view field, std::string detail) {
    return core::Error{core::ErrorCode::InvalidInput, std::move(detail), std::string(field)};
}

std::string_view problemField(core::MatchEventProblem problem) {
    switch (problem) {
    case core::MatchEventProblem::UnknownPlayer:
        return "player_unknown";
    case core::MatchEventProblem::MissingPlayer:
        return "player_missing";
    case core::MatchEventProblem::SamePlayer:
        return "same_player";
    case core::MatchEventProblem::NotOnPitch:
        return "player_not_on_pitch";
    case core::MatchEventProblem::NotOnBench:
        return "player_not_on_bench";
    case core::MatchEventProblem::AlreadySubstituted:
        return "player_substituted";
    case core::MatchEventProblem::SentOff:
        return "player_sent_off";
    case core::MatchEventProblem::MinuteOutOfRange:
        return "minute_out_of_range";
    case core::MatchEventProblem::AfterFinalMinute:
        return "after_final_minute";
    case core::MatchEventProblem::SecondYellowWithoutRed:
        return "second_yellow_without_red";
    }
    return "player_unknown";
}

core::Error eventError(core::MatchEventProblem problem, std::string_view context) {
    const std::string_view field = problemField(problem);
    return invalid(field, std::format("{}: {}", context, field));
}

// Stored events that do not fit the lineup: the history of the match is inconsistent (written
// outside Match Day, or damaged). Never shown as a lineup with 0:0 (P13 review).
core::Error historyError(const core::MatchEventError& error, std::string_view context) {
    return core::Error{core::ErrorCode::Database,
                       std::format("{}: stored event {} does not fit the match ({})", context,
                                   error.index, problemField(error.problem)),
                       std::string(kMatchHistory)};
}

std::vector<core::MatchEventDraft> draftsOf(const std::vector<core::MatchEvent>& events) {
    return {events.begin(), events.end()};
}

std::vector<core::MatchTacticChangeDraft>
draftsOf(const std::vector<core::MatchTacticChange>& changes) {
    return {changes.begin(), changes.end()};
}

// 11 slots, coordinates 0–1, at most one GK (also among the empty slots, so the stored
// `.tactic.json` stays valid).
bool isValidTacticShape(const core::Lineup& shape) {
    if (std::ssize(shape.pitchSlots) != core::kLineupSlots) {
        return false;
    }
    const auto inside = [](double value) {
        return std::isfinite(value) && value >= 0.0 && value <= 1.0;
    };
    return std::ranges::all_of(
               shape.pitchSlots,
               [&](const core::LineupSlot& slot) { return inside(slot.x) && inside(slot.y); }) &&
           std::ranges::count(shape.pitchSlots, core::Position::Gk, &core::LineupSlot::position) <=
               1;
}

core::MatchClock clockOf(const data::LiveMatchRow& row) {
    core::MatchClock clock;
    clock.format = row.format;
    for (const data::ClockStepRow& step : row.steps) {
        core::applyStep(clock, step.step, step.at);
    }
    clock.finished = row.finishedSeq.has_value();
    clock.endedEarlyAt = row.endedEarlyAt;
    clock.clockless = row.clockless;
    return clock;
}

// The highest seq of a match: the action entered last.
int lastSeqOf(const std::vector<core::MatchEvent>& events,
              const std::vector<core::MatchNote>& notes,
              const std::vector<core::MatchTacticChange>& tactics, const data::LiveMatchRow& row) {
    int seq = row.finishedSeq.value_or(0);
    for (const core::MatchTacticChange& change : tactics) {
        seq = std::max(seq, change.seq);
    }
    for (const core::MatchEvent& event : events) {
        seq = std::max(seq, event.seq.value_or(0));
    }
    for (const core::MatchNote& note : notes) {
        seq = std::max(seq, note.seq.value_or(0));
    }
    for (const data::ClockStepRow& step : row.steps) {
        seq = std::max(seq, step.seq);
    }
    return seq;
}

// What the action with the highest seq is.
std::optional<MatchDayAction> lastActionOf(const std::vector<core::MatchEvent>& events,
                                           const std::vector<core::MatchNote>& notes,
                                           const std::vector<core::MatchTacticChange>& tactics,
                                           const data::LiveMatchRow& row) {
    const int seq = lastSeqOf(events, notes, tactics, row);
    if (seq == 0) {
        return std::nullopt;
    }
    const auto tactic = std::ranges::find(tactics, seq, &core::MatchTacticChange::seq);
    if (tactic != tactics.end()) {
        MatchDayAction action{MatchDayAction::Kind::TacticChange, std::nullopt, std::nullopt,
                              std::nullopt, *tactic};
        return action;
    }
    std::optional<core::ClockStep> step;
    for (const data::ClockStepRow& taken : row.steps) {
        if (taken.seq == seq) {
            step = taken.step;
        }
    }
    if (row.finishedSeq == seq) {
        return MatchDayAction{MatchDayAction::Kind::Finish, std::nullopt, std::nullopt, step};
    }
    if (step) {
        return MatchDayAction{MatchDayAction::Kind::ClockStep, std::nullopt, std::nullopt, step};
    }
    // A second yellow is recognised by counting the cards in replay order (P13 review).
    const std::vector<bool> secondYellows = core::secondYellowReds(draftsOf(events));
    std::optional<core::MatchEvent> first;
    bool secondYellow = false;
    std::optional<core::MatchEvent> yellow;
    for (std::size_t index = 0; index < events.size(); ++index) {
        if (events[index].seq != seq) {
            continue;
        }
        if (!first) {
            first = events[index];
        }
        secondYellow = secondYellow || secondYellows[index];
        if (events[index].type == core::MatchEventType::Yellow) {
            yellow = events[index];
        }
    }
    if (secondYellow && yellow) {
        return MatchDayAction{MatchDayAction::Kind::SecondYellow, yellow, std::nullopt,
                              std::nullopt};
    }
    if (first) {
        return MatchDayAction{MatchDayAction::Kind::Event, first, std::nullopt, std::nullopt};
    }
    const auto note = std::ranges::find(notes, std::optional{seq}, &core::MatchNote::seq);
    if (note != notes.end()) {
        return MatchDayAction{MatchDayAction::Kind::Note, std::nullopt, *note, std::nullopt};
    }
    return std::nullopt;
}

} // namespace

struct MatchDayService::Live {
    core::Match match;
    data::LiveMatchRow row;
    core::MatchClock clock;
    core::Lineup lineup;
    core::MatchSheet sheet;
    std::vector<core::MatchEvent> events;
    std::vector<core::MatchNote> notes;
    std::vector<core::MatchTacticChange> tactics;
    core::MatchSituation situation;
};

MatchDayService::MatchDayService(data::DatabaseManager& db, data::MatchRepository& matches,
                                 data::MatchLiveRepository& live, data::MatchNoteRepository& notes,
                                 data::MatchTacticRepository& tactics,
                                 data::LineupRepository& lineups, data::ClubRepository& clubs,
                                 data::OpponentRepository& opponents,
                                 data::PlayerRepository& players, LineupService& lineupService,
                                 Session& session, const Clock& clock)
    : m_db(db), m_matches(matches), m_live(live), m_notes(notes), m_tactics(tactics),
      m_lineups(lineups), m_clubs(clubs), m_opponents(opponents), m_players(players),
      m_lineupService(lineupService), m_session(session), m_clock(clock) {}

core::Status MatchDayService::requireLogin() const {
    if (!m_session.user()) {
        return std::unexpected(
            core::Error{core::ErrorCode::PermissionDenied, "match day: nobody is logged in", {}});
    }
    return {};
}

core::Result<core::TeamCategory> MatchDayService::categoryOf(core::TeamId team) const {
    const auto club = m_clubs.getClub();
    if (!club) {
        return std::unexpected(club.error());
    }
    const auto teams = m_clubs.listTeams(club->id);
    if (!teams) {
        return std::unexpected(teams.error());
    }
    const auto found = std::ranges::find(*teams, team, &core::Team::id);
    return found == teams->end() ? core::TeamCategory::Other : found->category;
}

core::Result<std::optional<core::MatchId>> MatchDayService::liveMatch(core::TeamId team) const {
    if (auto allowed = requireLogin(); !allowed) {
        return std::unexpected(allowed.error());
    }
    return m_matches.findLive(team);
}

core::Result<core::Lineup> MatchDayService::lineupOf(core::MatchId match) const {
    auto lineup = m_lineups.findForMatch(match);
    if (!lineup) {
        return std::unexpected(lineup.error());
    }
    if (!*lineup) {
        return std::unexpected(conflict(kNoLineup, "match day: no saved lineup"));
    }
    return std::move(**lineup);
}

core::Result<MatchDayState> MatchDayService::state(core::MatchId matchId) const {
    if (auto allowed = requireLogin(); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto match = m_matches.get(matchId);
    if (!match) {
        return std::unexpected(match.error());
    }
    const auto opponent = m_opponents.get(match->opponentId);
    if (!opponent) {
        return std::unexpected(opponent.error());
    }
    const auto category = categoryOf(match->teamId);
    if (!category) {
        return std::unexpected(category.error());
    }
    MatchDayState state;
    state.match = CalendarMatch{*match, opponent->name};
    state.category = *category;

    const auto live = m_live.find(matchId);
    if (!live) {
        return std::unexpected(live.error());
    }
    if (*live) {
        state.clock = clockOf(**live);
    }
    const auto lineup = m_lineups.findForMatch(matchId);
    if (!lineup) {
        return std::unexpected(lineup.error());
    }
    if (*lineup) {
        state.sheet = core::matchSheetOf(**lineup);
        state.lineup = **lineup;
        state.hasLineup = true;
    }
    auto players = m_players.list(match->teamId, data::ArchivedPlayers::Include);
    if (!players) {
        return std::unexpected(players.error());
    }
    std::set<core::PlayerId> inSheet(state.sheet.bench.begin(), state.sheet.bench.end());
    for (const core::StartingPlayer& starter : state.sheet.starters) {
        inSheet.insert(starter.player);
    }
    for (core::Player& player : *players) {
        if (inSheet.contains(player.id)) {
            state.players.push_back(std::move(player));
        }
    }
    auto events = m_matches.listEvents(matchId);
    if (!events) {
        return std::unexpected(events.error());
    }
    state.events = std::move(*events);
    auto notes = m_notes.list(matchId);
    if (!notes) {
        return std::unexpected(notes.error());
    }
    state.notes = std::move(*notes);
    auto tactics = m_tactics.list(matchId);
    if (!tactics) {
        return std::unexpected(tactics.error());
    }
    state.tacticChanges = std::move(*tactics);
    const auto drafts = draftsOf(state.events);
    auto situation = core::replayMatch(state.sheet, drafts);
    state.shape = state.lineup;
    if (*live) {
        if (!situation) {
            return std::unexpected(historyError(situation.error(), "match day state"));
        }
        state.situation = std::move(*situation);
        state.lastAction = lastActionOf(state.events, state.notes, state.tacticChanges, **live);
        if (state.hasLineup) {
            auto shape = core::shapeAt(state.lineup, drafts, draftsOf(state.tacticChanges));
            if (!shape) {
                return std::unexpected(historyError(shape.error(), "match day shape"));
            }
            state.shape = std::move(*shape);
        }
    } else {
        // Data that Match Day did not write (an import, a match without a lineup): show the
        // lineup and the stored score.
        state.situation = situation ? std::move(*situation) : core::startingSituation(state.sheet);
        state.situation.goalsFor = match->goalsFor.value_or(0);
        state.situation.goalsAgainst = match->goalsAgainst.value_or(0);
    }
    return state;
}

core::Result<MatchDayStartCheck> MatchDayService::startCheck(core::MatchId matchId,
                                                             int yellowCardThreshold) const {
    if (auto allowed = requireLogin(); !allowed) {
        return std::unexpected(allowed.error());
    }
    const auto match = m_matches.get(matchId);
    if (!match) {
        return std::unexpected(match.error());
    }
    MatchDayStartCheck check;
    const auto category = categoryOf(match->teamId);
    if (!category) {
        return std::unexpected(category.error());
    }
    check.defaultFormat = core::defaultMatchFormat(*category);
    if (match->status != core::MatchStatus::Planned) {
        check.block = MatchDayStartCheck::Block::NotPlanned;
        return check;
    }
    const auto live = m_matches.findLive(match->teamId);
    if (!live) {
        return std::unexpected(live.error());
    }
    if (*live && **live != matchId) {
        check.block = MatchDayStartCheck::Block::AnotherLive;
        return check;
    }
    const auto saved = m_lineups.findForMatch(matchId);
    if (!saved) {
        return std::unexpected(saved.error());
    }
    if (!*saved) {
        check.block = MatchDayStartCheck::Block::NoLineup;
        return check;
    }
    const auto lineup = m_lineupService.matchLineup(matchId, yellowCardThreshold);
    if (!lineup) {
        return std::unexpected(lineup.error());
    }
    const auto issues = m_lineupService.check(lineup->lineup, lineup->context);
    check.lineupErrors = static_cast<int>(std::ranges::count(issues, core::LineupSeverity::Error,
                                                             &core::LineupIssue::severity)) +
                         lineup->droppedPlayers;
    if (check.lineupErrors > 0) {
        check.block = MatchDayStartCheck::Block::LineupErrors;
    }
    return check;
}

core::Status MatchDayService::start(core::MatchId match, core::MatchFormat format,
                                    int yellowCardThreshold, bool clockless) {
    if (auto allowed = m_session.require(core::Permission::RunMatchDay); !allowed) {
        return allowed;
    }
    if (!core::isValidMatchFormat(format)) {
        return std::unexpected(
            invalid(kMatchFormat,
                    std::format("match format {} x {}", format.periods, format.periodLength)));
    }
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto check = startCheck(match, yellowCardThreshold);
    if (!check) {
        return std::unexpected(check.error());
    }
    switch (check->block) {
    case MatchDayStartCheck::Block::None:
        break;
    case MatchDayStartCheck::Block::NotPlanned:
        return std::unexpected(conflict(kMatchNotPlanned, "match day start: not planned"));
    case MatchDayStartCheck::Block::AnotherLive:
        return std::unexpected(conflict(kAnotherLiveMatch, "match day start: another live match"));
    case MatchDayStartCheck::Block::NoLineup:
        return std::unexpected(conflict(kNoLineup, "match day start: no saved lineup"));
    case MatchDayStartCheck::Block::LineupErrors:
        return std::unexpected(conflict(kLineupErrors, "match day start: lineup has errors"));
    }
    if (auto created = m_live.create(match, format, clockless); !created) {
        return created;
    }
    if (auto status = m_matches.setStatus(match, core::MatchStatus::Live); !status) {
        return status;
    }
    const auto lineup = lineupOf(match);
    if (!lineup) {
        return std::unexpected(lineup.error());
    }
    if (const auto minutes = recompute(match, *lineup, std::nullopt); !minutes) {
        return std::unexpected(minutes.error());
    }
    return transaction->commit();
}

core::Result<MatchDayService::Live> MatchDayService::readLive(core::MatchId matchId,
                                                              bool allowFinished) const {
    auto match = m_matches.get(matchId);
    if (!match) {
        return std::unexpected(match.error());
    }
    const bool accepted = match->status == core::MatchStatus::Live ||
                          (allowFinished && match->status == core::MatchStatus::Finished);
    auto row = m_live.find(matchId);
    if (!row) {
        return std::unexpected(row.error());
    }
    if (!accepted || !*row) {
        return std::unexpected(conflict(kMatchNotLive, "match day: the match is not live"));
    }
    auto lineup = lineupOf(matchId);
    if (!lineup) {
        return std::unexpected(lineup.error());
    }
    core::MatchSheet sheet = core::matchSheetOf(*lineup);
    auto events = m_matches.listEvents(matchId);
    if (!events) {
        return std::unexpected(events.error());
    }
    auto notes = m_notes.list(matchId);
    if (!notes) {
        return std::unexpected(notes.error());
    }
    auto tactics = m_tactics.list(matchId);
    if (!tactics) {
        return std::unexpected(tactics.error());
    }
    const auto drafts = draftsOf(*events);
    auto situation = core::replayMatch(sheet, drafts);
    if (!situation) {
        return std::unexpected(historyError(situation.error(), "replay stored events"));
    }
    core::MatchClock clock = clockOf(**row);
    return Live{std::move(*match),  std::move(**row),    std::move(clock),
                std::move(*lineup), std::move(sheet),    std::move(*events),
                std::move(*notes),  std::move(*tactics), std::move(*situation)};
}

core::Result<core::MatchMinutes> MatchDayService::recompute(core::MatchId match,
                                                            const core::Lineup& lineup,
                                                            std::optional<int> finalMinute) {
    const auto events = m_matches.listEvents(match);
    if (!events) {
        return std::unexpected(events.error());
    }
    const auto tactics = m_tactics.list(match);
    if (!tactics) {
        return std::unexpected(tactics.error());
    }
    const auto drafts = draftsOf(*events);
    auto minutes = core::computeMatchMinutes(lineup, drafts, draftsOf(*tactics), finalMinute);
    if (!minutes) {
        return std::unexpected(historyError(minutes.error(), "recompute minutes"));
    }
    // Ratings and RPE (packet P14) belong to the player, not to the minutes: they stay.
    const auto existing = m_matches.listAppearances(match);
    if (!existing) {
        return std::unexpected(existing.error());
    }
    for (core::AppearanceDraft& appearance : minutes->appearances) {
        const auto old =
            std::ranges::find(*existing, appearance.playerId, &core::AppearanceDraft::playerId);
        if (old != existing->end()) {
            appearance.rating = old->rating;
            appearance.rpe = old->rpe;
        }
    }
    if (auto replaced = m_matches.replaceAppearances(match, minutes->appearances); !replaced) {
        return std::unexpected(replaced.error());
    }
    return std::move(*minutes);
}

core::Status MatchDayService::endNow(const Live& live, int seq, std::optional<int> minute) {
    const core::Timestamp now = m_clock.now();
    core::MatchClock clock = live.clock;
    if (core::onPitchCount(live.situation) < core::kMinPlayersOnPitch) {
        // Without the clock the minute typed counts too, never before the last event.
        clock.endedEarlyAt = std::max(core::earlyEndMinute(live.clock, now, draftsOf(live.events)),
                                      live.clock.clockless ? minute.value_or(0) : 0);
    }
    if (core::phaseOf(clock) == core::MatchPhase::Running && !clock.clockless) {
        const core::ClockStep end{core::currentPeriod(clock), core::ClockStepKind::End};
        if (auto appended = m_live.appendStep(live.match.id, end, now, seq); !appended) {
            return appended;
        }
        core::applyStep(clock, end, now);
    }
    clock.finished = true;
    if (auto finished = m_live.setFinished(live.match.id, seq, clock.endedEarlyAt); !finished) {
        return finished;
    }
    const auto minutes = recompute(live.match.id, live.lineup, core::finalMinute(clock));
    if (!minutes) {
        return std::unexpected(minutes.error());
    }
    return m_matches.setResult(live.match.id, minutes->goalsFor, minutes->goalsAgainst,
                               core::MatchStatus::Finished);
}

core::Status MatchDayService::cancelStart(core::MatchId match) {
    if (auto allowed = m_session.require(core::Permission::RunMatchDay); !allowed) {
        return allowed;
    }
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto live = readLive(match, false);
    if (!live) {
        return std::unexpected(live.error());
    }
    if ((core::phaseOf(live->clock) != core::MatchPhase::NotStarted && !live->clock.clockless) ||
        !live->events.empty() || !live->notes.empty() || !live->tactics.empty()) {
        return std::unexpected(conflict(kMatchStarted, "match day: the match has kicked off"));
    }
    if (auto removed = m_live.remove(match); !removed) {
        return removed;
    }
    if (auto status = m_matches.setStatus(match, core::MatchStatus::Planned); !status) {
        return status;
    }
    if (auto cleared = m_matches.replaceAppearances(match, {}); !cleared) {
        return cleared;
    }
    return transaction->commit();
}

core::Status MatchDayService::advanceClock(core::MatchId match) {
    if (auto allowed = m_session.require(core::Permission::RunMatchDay); !allowed) {
        return allowed;
    }
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto live = readLive(match, false);
    if (!live) {
        return std::unexpected(live.error());
    }
    if (live->clock.clockless) {
        return std::unexpected(conflict(kClockless, "match day: no clock"));
    }
    const auto step = core::nextStep(live->clock);
    if (!step) {
        return std::unexpected(conflict(kMatchNotLive, "match day: the match is finished"));
    }
    const bool last =
        step->kind == core::ClockStepKind::End && step->period == live->clock.format.periods;
    if (!last && core::onPitchCount(live->situation) < core::kMinPlayersOnPitch) {
        return std::unexpected(conflict(kTooFewPlayers, "match day: fewer than 7 players"));
    }
    const auto seq = m_live.takeSeq(match);
    if (!seq) {
        return std::unexpected(seq.error());
    }
    // The end of the last part is the end of the match: the one way it ends.
    if (last) {
        if (auto ended = endNow(*live, *seq, std::nullopt); !ended) {
            return ended;
        }
    } else if (auto appended = m_live.appendStep(match, *step, m_clock.now(), *seq); !appended) {
        return appended;
    }
    return transaction->commit();
}

core::Status MatchDayService::endMatch(core::MatchId match, std::optional<int> minute) {
    if (auto allowed = m_session.require(core::Permission::RunMatchDay); !allowed) {
        return allowed;
    }
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto live = readLive(match, false);
    if (!live) {
        return std::unexpected(live.error());
    }
    const core::MatchPhase phase = core::phaseOf(live->clock);
    if (phase == core::MatchPhase::NotStarted) {
        return std::unexpected(conflict(kMatchNotRunning, "match day: before the kick-off"));
    }
    if (minute && (*minute < 0 || *minute > live->clock.format.totalMinutes())) {
        return std::unexpected(
            invalid(kMinuteOutOfMatch, "match day: end minute out of the match"));
    }
    // With 7 players or more only the end of the last part ends the match, as the clock button;
    // without the clock the end is always there.
    const bool lastPart = phase == core::MatchPhase::Running &&
                          core::currentPeriod(live->clock) == live->clock.format.periods;
    if (core::onPitchCount(live->situation) >= core::kMinPlayersOnPitch && !lastPart &&
        !live->clock.clockless) {
        return std::unexpected(
            conflict(kMatchNotOver, "match day: 7 players or more, not the end"));
    }
    const auto seq = m_live.takeSeq(match);
    if (!seq) {
        return std::unexpected(seq.error());
    }
    if (auto ended = endNow(*live, *seq, minute); !ended) {
        return ended;
    }
    return transaction->commit();
}

core::Result<core::Timestamp> MatchDayService::setClock(core::MatchId match,
                                                        std::chrono::seconds elapsed) {
    if (auto allowed = m_session.require(core::Permission::RunMatchDay); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto live = readLive(match, false);
    if (!live) {
        return std::unexpected(live.error());
    }
    if (live->clock.clockless || core::phaseOf(live->clock) != core::MatchPhase::Running) {
        return std::unexpected(conflict(kClockNotRunning, "match day: no part runs"));
    }
    const int length = live->clock.format.periodLength;
    if (elapsed < std::chrono::seconds{0} ||
        elapsed > std::chrono::minutes{length + core::kMaxAddedTime}) {
        return std::unexpected(invalid(kClockTime, "match day: clock out of the part"));
    }
    const core::Timestamp previous = live->clock.periods.back().start;
    if (auto moved = moveStart(*live, m_clock.now() - elapsed); !moved) {
        return std::unexpected(moved.error());
    }
    if (auto committed = transaction->commit(); !committed) {
        return std::unexpected(committed.error());
    }
    return previous;
}

core::Status MatchDayService::restoreClock(core::MatchId match, core::Timestamp start) {
    if (auto allowed = m_session.require(core::Permission::RunMatchDay); !allowed) {
        return allowed;
    }
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto live = readLive(match, false);
    if (!live) {
        return std::unexpected(live.error());
    }
    if (live->clock.clockless || core::phaseOf(live->clock) != core::MatchPhase::Running) {
        return std::unexpected(conflict(kClockNotRunning, "match day: no part runs"));
    }
    if (auto moved = moveStart(*live, start); !moved) {
        return moved;
    }
    return transaction->commit();
}

core::Status MatchDayService::moveStart(const Live& live, core::Timestamp start) {
    const int period = core::currentPeriod(live.clock);
    const auto least = core::minElapsedForEvents(live.clock.format, period, draftsOf(live.events));
    if (m_clock.now() - start < least) {
        return std::unexpected(
            invalid(kClockBeforeEvent, "match day: the clock before an event of the part"));
    }
    return m_live.moveStep(live.match.id, {period, core::ClockStepKind::Start}, start);
}

core::Result<core::MatchTime> MatchDayService::timeFor(const Live& live,
                                                       std::optional<core::MatchTime> time,
                                                       bool substitution) const {
    if (core::phaseOf(live.clock) == core::MatchPhase::NotStarted) {
        return std::unexpected(conflict(kMatchNotRunning, "match day: before the kick-off"));
    }
    if (live.clock.clockless) {
        // Without the clock every action takes a minute typed, any minute of the format.
        if (!time) {
            return std::unexpected(invalid(kMinuteRequired, "match day: no minute without clock"));
        }
        if (!core::isValidMatchTime(live.clock.format, *time)) {
            return std::unexpected(
                invalid(kMinuteOutOfMatch, "match day: minute outside the match format"));
        }
        return *time;
    }
    const core::Timestamp now = m_clock.now();
    const core::MatchTime chosen = time.value_or(core::defaultTime(live.clock, now, substitution));
    if (!core::isPlayedTime(live.clock, now, chosen, substitution)) {
        return std::unexpected(
            invalid(kMinuteNotPlayed, std::format("match day: minute {}+{} not played",
                                                  chosen.minute, chosen.addedTime.value_or(0))));
    }
    return chosen;
}

core::Result<std::vector<core::MatchEventDraft>>
MatchDayService::prepareEvents(const Live& live, const core::MatchEventDraft& event,
                               std::optional<EventConflict>& conflictFound) const {
    std::vector<core::MatchEventDraft> all = draftsOf(live.events);
    const std::size_t stored = all.size();
    std::vector<core::MatchEventDraft> rows{event};
    // A second yellow at its time (counted in the order of the match) brings its red row.
    if (event.type == core::MatchEventType::Yellow && event.playerId) {
        all.push_back(event);
        int earlier = 0;
        for (const std::size_t index : core::replayOrder(all)) {
            if (index == stored) {
                break;
            }
            if (all[index].type == core::MatchEventType::Yellow &&
                all[index].playerId == event.playerId) {
                ++earlier;
            }
        }
        all.pop_back();
        if (earlier == 1) {
            core::MatchEventDraft red = event;
            red.type = core::MatchEventType::Red;
            red.relatedPlayerId.reset();
            rows.push_back(red);
        }
    }
    all.insert(all.end(), rows.begin(), rows.end());
    const auto replayed = core::replayMatch(live.sheet, all);
    if (!replayed) {
        const core::MatchEventError& error = replayed.error();
        if (error.index >= stored) {
            conflictFound = EventConflict{error.problem, std::nullopt};
            return std::unexpected(eventError(error.problem, "record match event"));
        }
        conflictFound = EventConflict{error.problem, live.events[error.index]};
        return std::unexpected(
            conflict(kEventConflict,
                     std::format("match day: a later event {} no longer fits ({})",
                                 live.events[error.index].id.value, problemField(error.problem))));
    }
    return rows;
}

core::Result<std::optional<EventConflict>>
MatchDayService::checkEvent(core::MatchId match, const core::MatchEventDraft& event) const {
    if (auto allowed = requireLogin(); !allowed) {
        return std::unexpected(allowed.error());
    }
    const auto live = readLive(match, false);
    if (!live) {
        return std::unexpected(live.error());
    }
    std::optional<EventConflict> found;
    if (const auto rows = prepareEvents(*live, event, found); !rows && !found) {
        return std::unexpected(rows.error());
    }
    return found;
}

core::Status MatchDayService::recordEvent(core::MatchId match, core::MatchEventDraft event,
                                          std::optional<core::MatchTime> time) {
    if (auto allowed = m_session.require(core::Permission::RunMatchDay); !allowed) {
        return allowed;
    }
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto live = readLive(match, false);
    if (!live) {
        return std::unexpected(live.error());
    }
    if (core::onPitchCount(live->situation) < core::kMinPlayersOnPitch) {
        return std::unexpected(conflict(kTooFewPlayers, "match day: fewer than 7 players"));
    }
    const auto chosen = timeFor(*live, time, event.type == core::MatchEventType::Sub);
    if (!chosen) {
        return std::unexpected(chosen.error());
    }
    event.minute = chosen->minute;
    event.addedTime = chosen->addedTime;
    std::optional<EventConflict> found;
    const auto rows = prepareEvents(*live, event, found);
    if (!rows) {
        return std::unexpected(rows.error());
    }
    const auto seq = m_live.takeSeq(match);
    if (!seq) {
        return std::unexpected(seq.error());
    }
    for (const core::MatchEventDraft& row : *rows) {
        if (auto appended = m_matches.appendEvent(match, row, *seq); !appended) {
            return std::unexpected(appended.error());
        }
    }
    if (const auto minutes = recompute(match, live->lineup, std::nullopt); !minutes) {
        return std::unexpected(minutes.error());
    }
    return transaction->commit();
}

core::Status MatchDayService::recordGoal(core::MatchId match, core::PlayerId scorer,
                                         std::optional<core::PlayerId> assistant,
                                         std::optional<core::MatchTime> time) {
    core::MatchEventDraft goal;
    goal.type = core::MatchEventType::Goal;
    goal.playerId = scorer;
    goal.relatedPlayerId = assistant;
    return recordEvent(match, goal, time);
}

core::Status MatchDayService::recordOpponentGoal(core::MatchId match,
                                                 std::optional<core::MatchTime> time) {
    core::MatchEventDraft goal;
    goal.type = core::MatchEventType::OpponentGoal;
    return recordEvent(match, goal, time);
}

core::Status MatchDayService::recordYellow(core::MatchId match, core::PlayerId player,
                                           std::optional<core::MatchTime> time) {
    core::MatchEventDraft card;
    card.type = core::MatchEventType::Yellow;
    card.playerId = player;
    return recordEvent(match, card, time);
}

core::Status MatchDayService::recordRed(core::MatchId match, core::PlayerId player,
                                        std::optional<core::MatchTime> time) {
    core::MatchEventDraft card;
    card.type = core::MatchEventType::Red;
    card.playerId = player;
    return recordEvent(match, card, time);
}

core::Status MatchDayService::recordSubstitution(core::MatchId match, core::PlayerId comingOn,
                                                 core::PlayerId goingOff,
                                                 std::optional<core::MatchTime> time) {
    core::MatchEventDraft sub;
    sub.type = core::MatchEventType::Sub;
    sub.playerId = comingOn;
    sub.relatedPlayerId = goingOff;
    return recordEvent(match, sub, time);
}

core::Status MatchDayService::checkNote(const Live& live, std::optional<core::PlayerId> player,
                                        std::string_view text) const {
    const std::size_t length = core::characterCount(core::trimmed(text));
    if (length == 0 || length > static_cast<std::size_t>(core::kMaxNoteLength)) {
        return std::unexpected(invalid(kNoteText, "match note: empty or too long"));
    }
    if (player && live.situation.find(*player) == nullptr) {
        return std::unexpected(invalid("player_unknown", "match note: player not in the lineup"));
    }
    return {};
}

core::Result<core::MatchNoteId> MatchDayService::addNote(core::MatchId match,
                                                         std::optional<core::MatchTime> time,
                                                         std::optional<core::PlayerId> player,
                                                         std::string_view text) {
    if (auto allowed = m_session.require(core::Permission::RunMatchDay); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto live = readLive(match, false);
    if (!live) {
        return std::unexpected(live.error());
    }
    const auto chosen = timeFor(*live, time, false);
    if (!chosen) {
        return std::unexpected(chosen.error());
    }
    if (auto checked = checkNote(*live, player, text); !checked) {
        return std::unexpected(checked.error());
    }
    const auto seq = m_live.takeSeq(match);
    if (!seq) {
        return std::unexpected(seq.error());
    }
    const core::MatchNoteDraft draft{chosen->minute, chosen->addedTime, player,
                                     core::trimmed(text)};
    const auto id = m_notes.add(match, draft, *seq);
    if (!id) {
        return std::unexpected(id.error());
    }
    if (auto committed = transaction->commit(); !committed) {
        return std::unexpected(committed.error());
    }
    return *id;
}

core::Status MatchDayService::updateNote(core::MatchId match, core::MatchNoteId note,
                                         core::MatchTime time, std::optional<core::PlayerId> player,
                                         std::string_view text) {
    if (auto allowed = m_session.require(core::Permission::RunMatchDay); !allowed) {
        return allowed;
    }
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto live = readLive(match, false);
    if (!live) {
        return std::unexpected(live.error());
    }
    const auto chosen = timeFor(*live, time, false);
    if (!chosen) {
        return std::unexpected(chosen.error());
    }
    if (auto checked = checkNote(*live, player, text); !checked) {
        return checked;
    }
    const core::MatchNoteDraft draft{chosen->minute, chosen->addedTime, player,
                                     core::trimmed(text)};
    if (auto updated = m_notes.update(match, note, draft); !updated) {
        return updated;
    }
    return transaction->commit();
}

core::Result<core::MatchNote> MatchDayService::deleteNote(core::MatchId match,
                                                          core::MatchNoteId note) {
    if (auto allowed = m_session.require(core::Permission::RunMatchDay); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    if (const auto live = readLive(match, false); !live) {
        return std::unexpected(live.error());
    }
    auto removed = m_notes.remove(match, note);
    if (!removed) {
        return std::unexpected(removed.error());
    }
    if (auto committed = transaction->commit(); !committed) {
        return std::unexpected(committed.error());
    }
    return removed;
}

core::Result<core::MatchNoteId> MatchDayService::restoreNote(const core::MatchNote& note) {
    if (auto allowed = m_session.require(core::Permission::RunMatchDay); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    if (const auto live = readLive(note.matchId, false); !live) {
        return std::unexpected(live.error());
    }
    const auto id = m_notes.add(note.matchId, note, note.seq);
    if (!id) {
        return std::unexpected(id.error());
    }
    if (auto committed = transaction->commit(); !committed) {
        return std::unexpected(committed.error());
    }
    return *id;
}

core::Result<core::MatchTacticChangeId>
MatchDayService::recordTacticChange(core::MatchId match, const core::Lineup& shape,
                                    std::optional<core::MatchTime> time) {
    if (auto allowed = m_session.require(core::Permission::RunMatchDay); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto live = readLive(match, false);
    if (!live) {
        return std::unexpected(live.error());
    }
    if (core::onPitchCount(live->situation) < core::kMinPlayersOnPitch) {
        return std::unexpected(conflict(kTooFewPlayers, "match day: fewer than 7 players"));
    }
    const auto chosen = timeFor(*live, time, true);
    if (!chosen) {
        return std::unexpected(chosen.error());
    }
    if (!isValidTacticShape(shape)) {
        return std::unexpected(invalid(kTacticShape, "match day: not a shape of 11 slots"));
    }
    const auto key = [](int minute, std::optional<int> added) {
        return std::tuple{minute, added ? 0 : 1, added.value_or(0)};
    };
    for (const core::MatchTacticChange& stored : live->tactics) {
        if (key(stored.minute, stored.addedTime) > key(chosen->minute, chosen->addedTime)) {
            return std::unexpected(
                conflict(kTacticChangeLater,
                         std::format("match day: tactic change {} is later", stored.id.value)));
        }
    }
    const auto events = draftsOf(live->events);
    std::vector<core::MatchTacticChangeDraft> changes = draftsOf(live->tactics);
    const auto before = core::shapeAt(live->lineup, events, changes, *chosen);
    if (!before) {
        return std::unexpected(historyError(before.error(), "tactic change"));
    }
    core::MatchTacticChangeDraft change{chosen->minute, chosen->addedTime, shape};
    change.shape.bench.clear();
    for (core::LineupSlot& slot : change.shape.pitchSlots) {
        slot.player.reset();
    }
    changes.push_back(change);
    const auto after = core::shapeAt(live->lineup, events, changes, *chosen);
    if (!after) {
        return std::unexpected(historyError(after.error(), "tactic change"));
    }
    if (*after == *before) {
        return std::unexpected(invalid(kTacticUnchanged, "match day: the shape is the same"));
    }
    change.shape.formation = after->formation;
    const auto seq = m_live.takeSeq(match);
    if (!seq) {
        return std::unexpected(seq.error());
    }
    const auto id = m_tactics.create(match, change, *seq);
    if (!id) {
        return std::unexpected(id.error());
    }
    if (const auto minutes = recompute(match, live->lineup, std::nullopt); !minutes) {
        return std::unexpected(minutes.error());
    }
    if (auto committed = transaction->commit(); !committed) {
        return std::unexpected(committed.error());
    }
    return *id;
}

core::Result<MatchDayAction> MatchDayService::undoLast(core::MatchId match) {
    if (auto allowed = m_session.require(core::Permission::RunMatchDay); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto live = readLive(match, true);
    if (!live) {
        return std::unexpected(live.error());
    }
    const auto action = lastActionOf(live->events, live->notes, live->tactics, live->row);
    if (!action) {
        return std::unexpected(core::Error{core::ErrorCode::NotFound, "match day: nothing to undo",
                                           std::string(kNothingToUndo)});
    }
    const int seq = lastSeqOf(live->events, live->notes, live->tactics, live->row);
    if (action->kind == MatchDayAction::Kind::Finish) {
        // A finished match made live again next to another live match of the team: two live
        // matches, and the start of the other one would refuse that (P13 review).
        const auto other = m_matches.findLive(live->match.teamId);
        if (!other) {
            return std::unexpected(other.error());
        }
        if (*other && **other != match) {
            return std::unexpected(conflict(
                kUndoEndAnotherLive,
                std::format("match day undo: match {} of the team is live", (*other)->value)));
        }
        if (auto reopened = m_live.setFinished(match, std::nullopt); !reopened) {
            return std::unexpected(reopened.error());
        }
        if (auto result =
                m_matches.setResult(match, std::nullopt, std::nullopt, core::MatchStatus::Live);
            !result) {
            return std::unexpected(result.error());
        }
    }
    if (const auto steps = m_live.deleteSteps(match, seq); !steps) {
        return std::unexpected(steps.error());
    }
    if (const auto events = m_matches.deleteEventsWithSeq(match, seq); !events) {
        return std::unexpected(events.error());
    }
    if (const auto notes = m_notes.deleteWithSeq(match, seq); !notes) {
        return std::unexpected(notes.error());
    }
    if (const auto tactics = m_tactics.deleteWithSeq(match, seq); !tactics) {
        return std::unexpected(tactics.error());
    }
    if (const auto minutes = recompute(match, live->lineup, std::nullopt); !minutes) {
        return std::unexpected(minutes.error());
    }
    if (auto committed = transaction->commit(); !committed) {
        return std::unexpected(committed.error());
    }
    return *action;
}

} // namespace fdss::services
