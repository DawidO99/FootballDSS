#include "services/MatchDayService.hpp"

#include "core/algorithms/LineupValidator.hpp"
#include "data/db/Transaction.hpp"
#include "data/repositories/ClubRepository.hpp"
#include "data/repositories/LineupRepository.hpp"
#include "data/repositories/MatchLiveRepository.hpp"
#include "data/repositories/MatchRepository.hpp"
#include "data/repositories/OpponentRepository.hpp"
#include "data/repositories/PlayerRepository.hpp"
#include "services/Clock.hpp"
#include "services/LineupService.hpp"
#include "services/Session.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
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
constexpr std::string_view kHalfLength = "half_length";
constexpr std::string_view kNothingToUndo = "nothing_to_undo";

core::Error conflict(std::string_view field, std::string detail) {
    return core::Error{core::ErrorCode::Conflict, std::move(detail), std::string(field)};
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
    }
    return "player_unknown";
}

core::Error eventError(core::MatchEventProblem problem, std::string_view context) {
    const std::string_view field = problemField(problem);
    return core::Error{core::ErrorCode::InvalidInput, std::format("{}: {}", context, field),
                       std::string(field)};
}

std::vector<core::MatchEventDraft> draftsOf(const std::vector<core::MatchEvent>& events) {
    return {events.begin(), events.end()};
}

core::MatchClock clockOf(const data::LiveMatchRow& row) {
    core::MatchClock clock;
    clock.halfLength = row.halfLength;
    for (const data::ClockStepRow& step : row.steps) {
        core::setStep(clock, step.step, step.at);
    }
    return clock;
}

std::optional<core::MatchEventId> lastEventId(const std::vector<core::MatchEvent>& events) {
    const auto last = std::ranges::max_element(events, {}, &core::MatchEvent::id);
    return last == events.end() ? std::nullopt : std::optional{last->id};
}

// The red card of a second yellow: a red row without a related player, recorded right after a
// yellow card of the same player at the same time, who has two yellow cards.
std::optional<core::MatchEvent> secondYellowOf(const std::vector<core::MatchEvent>& events,
                                               const core::MatchEvent& red) {
    if (red.type != core::MatchEventType::Red || red.relatedPlayerId || !red.playerId) {
        return std::nullopt;
    }
    std::optional<core::MatchEvent> previous;
    int yellows = 0;
    for (const core::MatchEvent& event : events) {
        if (event.type == core::MatchEventType::Yellow && event.playerId == red.playerId) {
            ++yellows;
        }
        if (event.id < red.id && (!previous || event.id > previous->id)) {
            previous = event;
        }
    }
    if (yellows >= 2 && previous && previous->type == core::MatchEventType::Yellow &&
        previous->playerId == red.playerId && previous->minute == red.minute &&
        previous->addedTime == red.addedTime) {
        return previous;
    }
    return std::nullopt;
}

std::optional<MatchDayAction> lastActionOf(const std::vector<core::MatchEvent>& events,
                                           const std::vector<data::ClockStepRow>& steps) {
    const auto lastEvent = std::ranges::max_element(events, {}, &core::MatchEvent::id);
    const data::ClockStepRow* lastStep = steps.empty() ? nullptr : &steps.back();
    if (lastEvent != events.end() &&
        (lastStep == nullptr || !lastStep->afterEvent || lastEvent->id > *lastStep->afterEvent)) {
        if (auto yellow = secondYellowOf(events, *lastEvent)) {
            return MatchDayAction{MatchDayAction::Kind::SecondYellow, yellow, std::nullopt};
        }
        return MatchDayAction{MatchDayAction::Kind::Event, *lastEvent, std::nullopt};
    }
    if (lastStep != nullptr) {
        return MatchDayAction{MatchDayAction::Kind::ClockStep, std::nullopt, lastStep->step};
    }
    return std::nullopt;
}

} // namespace

struct MatchDayService::Live {
    core::Match match;
    data::LiveMatchRow row;
    core::MatchClock clock;
    core::MatchSheet sheet;
    std::vector<core::MatchEvent> events;
};

MatchDayService::MatchDayService(data::DatabaseManager& db, data::MatchRepository& matches,
                                 data::MatchLiveRepository& live, data::LineupRepository& lineups,
                                 data::ClubRepository& clubs, data::OpponentRepository& opponents,
                                 data::PlayerRepository& players, LineupService& lineupService,
                                 Session& session, const Clock& clock)
    : m_db(db), m_matches(matches), m_live(live), m_lineups(lineups), m_clubs(clubs),
      m_opponents(opponents), m_players(players), m_lineupService(lineupService),
      m_session(session), m_clock(clock) {}

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

core::Result<core::MatchSheet> MatchDayService::sheetOf(core::MatchId match) const {
    const auto lineup = m_lineups.findForMatch(match);
    if (!lineup) {
        return std::unexpected(lineup.error());
    }
    if (!*lineup) {
        return std::unexpected(conflict(kNoLineup, "match day: no saved lineup"));
    }
    return core::matchSheetOf(**lineup);
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
    const auto drafts = draftsOf(state.events);
    auto situation = core::replayMatch(state.sheet, drafts);
    if (*live && situation) {
        state.situation = std::move(*situation);
    } else {
        // Data that Match Day did not write (an import, a match without a lineup): show the
        // lineup and the stored score.
        state.situation = situation ? std::move(*situation) : core::startingSituation(state.sheet);
        state.situation.goalsFor = match->goalsFor.value_or(0);
        state.situation.goalsAgainst = match->goalsAgainst.value_or(0);
    }
    if (*live) {
        state.lastAction = lastActionOf(state.events, (*live)->steps);
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
    check.defaultHalfLength = core::defaultHalfLength(*category);
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

core::Status MatchDayService::start(core::MatchId match, int halfLength, int yellowCardThreshold) {
    if (auto allowed = m_session.require(core::Permission::RunMatchDay); !allowed) {
        return allowed;
    }
    if (!core::isValidHalfLength(halfLength)) {
        return std::unexpected(core::Error{core::ErrorCode::InvalidInput,
                                           std::format("half length {}", halfLength),
                                           std::string(kHalfLength)});
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
    if (auto created = m_live.create(match, halfLength); !created) {
        return created;
    }
    if (auto status = m_matches.setStatus(match, core::MatchStatus::Live); !status) {
        return status;
    }
    const auto sheet = sheetOf(match);
    if (!sheet) {
        return std::unexpected(sheet.error());
    }
    if (const auto minutes = recompute(match, *sheet, std::nullopt); !minutes) {
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
    const auto row = m_live.find(matchId);
    if (!row) {
        return std::unexpected(row.error());
    }
    if (!accepted || !*row) {
        return std::unexpected(conflict(kMatchNotLive, "match day: the match is not live"));
    }
    auto sheet = sheetOf(matchId);
    if (!sheet) {
        return std::unexpected(sheet.error());
    }
    auto events = m_matches.listEvents(matchId);
    if (!events) {
        return std::unexpected(events.error());
    }
    Live live{std::move(*match), **row, clockOf(**row), std::move(*sheet), std::move(*events)};
    return live;
}

core::Result<core::MatchMinutes> MatchDayService::recompute(core::MatchId match,
                                                            const core::MatchSheet& sheet,
                                                            std::optional<int> finalMinute) {
    const auto events = m_matches.listEvents(match);
    if (!events) {
        return std::unexpected(events.error());
    }
    const auto drafts = draftsOf(*events);
    auto minutes = core::computeMatchMinutes(sheet, drafts, finalMinute);
    if (!minutes) {
        return std::unexpected(eventError(minutes.error().problem, "recompute minutes"));
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
    if (core::phaseOf(live->clock) != core::MatchPhase::NotStarted || !live->events.empty()) {
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
    // A live match is never finished: the end of the match makes it finished.
    const core::ClockStep step = core::nextStep(live->clock).value_or(core::ClockStep::MatchEnd);
    if (auto appended = m_live.appendStep(match, step, m_clock.now(), lastEventId(live->events));
        !appended) {
        return appended;
    }
    if (step == core::ClockStep::MatchEnd) {
        const auto minutes = recompute(match, live->sheet, 2 * live->clock.halfLength);
        if (!minutes) {
            return std::unexpected(minutes.error());
        }
        if (auto result = m_matches.setResult(match, minutes->goalsFor, minutes->goalsAgainst,
                                              core::MatchStatus::Finished);
            !result) {
            return result;
        }
    }
    return transaction->commit();
}

core::Status MatchDayService::recordEvents(core::MatchId match,
                                           std::vector<core::MatchEventDraft> events,
                                           bool allowedAtHalfTime) {
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
    const core::ClockReading reading = core::readClock(live->clock, m_clock.now());
    const bool running = reading.phase == core::MatchPhase::FirstHalf ||
                         reading.phase == core::MatchPhase::SecondHalf ||
                         (allowedAtHalfTime && reading.phase == core::MatchPhase::HalfTime);
    if (!running) {
        return std::unexpected(conflict(kMatchNotRunning, "match day: no half is running"));
    }
    const auto drafts = draftsOf(live->events);
    auto situation = core::replayMatch(live->sheet, drafts);
    if (!situation) {
        return std::unexpected(eventError(situation.error().problem, "replay stored events"));
    }
    std::vector<core::MatchEventDraft> written;
    for (core::MatchEventDraft& event : events) {
        event.minute = reading.eventTime.minute;
        event.addedTime = reading.eventTime.addedTime;
        if (auto problem = core::applyEvent(*situation, event)) {
            return std::unexpected(eventError(*problem, "record match event"));
        }
        written.push_back(event);
        // A second yellow card sends the player off: a red row at the same time.
        if (event.type == core::MatchEventType::Yellow) {
            const core::PlayerInMatch* player = situation->find(*event.playerId);
            if (player != nullptr && player->yellowCards >= 2) {
                core::MatchEventDraft red = event;
                red.type = core::MatchEventType::Red;
                red.relatedPlayerId.reset();
                if (auto problem = core::applyEvent(*situation, red)) {
                    return std::unexpected(eventError(*problem, "record second yellow"));
                }
                written.push_back(red);
            }
        }
    }
    for (const core::MatchEventDraft& event : written) {
        if (auto appended = m_matches.appendEvent(match, event); !appended) {
            return std::unexpected(appended.error());
        }
    }
    if (const auto minutes = recompute(match, live->sheet, std::nullopt); !minutes) {
        return std::unexpected(minutes.error());
    }
    return transaction->commit();
}

core::Status MatchDayService::recordGoal(core::MatchId match, core::PlayerId scorer,
                                         std::optional<core::PlayerId> assistant) {
    core::MatchEventDraft goal;
    goal.type = core::MatchEventType::Goal;
    goal.playerId = scorer;
    goal.relatedPlayerId = assistant;
    return recordEvents(match, {goal}, false);
}

core::Status MatchDayService::recordOpponentGoal(core::MatchId match) {
    core::MatchEventDraft goal;
    goal.type = core::MatchEventType::OpponentGoal;
    return recordEvents(match, {goal}, false);
}

core::Status MatchDayService::recordYellow(core::MatchId match, core::PlayerId player) {
    core::MatchEventDraft card;
    card.type = core::MatchEventType::Yellow;
    card.playerId = player;
    return recordEvents(match, {card}, false);
}

core::Status MatchDayService::recordRed(core::MatchId match, core::PlayerId player) {
    core::MatchEventDraft card;
    card.type = core::MatchEventType::Red;
    card.playerId = player;
    return recordEvents(match, {card}, false);
}

core::Status MatchDayService::recordSubstitution(core::MatchId match, core::PlayerId comingOn,
                                                 core::PlayerId goingOff) {
    core::MatchEventDraft sub;
    sub.type = core::MatchEventType::Sub;
    sub.playerId = comingOn;
    sub.relatedPlayerId = goingOff;
    return recordEvents(match, {sub}, true);
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
    const auto action = lastActionOf(live->events, live->row.steps);
    if (!action) {
        return std::unexpected(core::Error{core::ErrorCode::NotFound, "match day: nothing to undo",
                                           std::string(kNothingToUndo)});
    }
    switch (action->kind) {
    case MatchDayAction::Kind::SecondYellow:
        // The red row, then the yellow one below.
        if (auto deleted = m_matches.deleteLastEvent(match); !deleted) {
            return std::unexpected(deleted.error());
        }
        [[fallthrough]];
    case MatchDayAction::Kind::Event:
        if (auto deleted = m_matches.deleteLastEvent(match); !deleted) {
            return std::unexpected(deleted.error());
        }
        break;
    case MatchDayAction::Kind::ClockStep:
        if (auto deleted = m_live.deleteLastStep(match); !deleted) {
            return std::unexpected(deleted.error());
        }
        if (action->step == core::ClockStep::MatchEnd) {
            if (auto result =
                    m_matches.setResult(match, std::nullopt, std::nullopt, core::MatchStatus::Live);
                !result) {
                return std::unexpected(result.error());
            }
        }
        break;
    }
    if (const auto minutes = recompute(match, live->sheet, std::nullopt); !minutes) {
        return std::unexpected(minutes.error());
    }
    if (auto committed = transaction->commit(); !committed) {
        return std::unexpected(committed.error());
    }
    return *action;
}

} // namespace fdss::services
