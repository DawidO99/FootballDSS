#include "services/MatchReportService.hpp"

#include "core/algorithms/Formations.hpp"
#include "core/domain/Validation.hpp"
#include "data/db/Transaction.hpp"
#include "data/repositories/LineupRepository.hpp"
#include "data/repositories/MatchLiveRepository.hpp"
#include "data/repositories/MatchNoteRepository.hpp"
#include "data/repositories/MatchRepository.hpp"
#include "data/repositories/MatchTacticRepository.hpp"
#include "data/repositories/OpponentRepository.hpp"
#include "data/repositories/PlayerRepository.hpp"
#include "services/Session.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <ranges>
#include <set>
#include <string>
#include <utility>

namespace fdss::services {

namespace {

constexpr std::string_view kMatchNotFinished = "match_not_finished";
constexpr std::string_view kNotCorrectable = "not_correctable";
constexpr std::string_view kEventDependent = "event_dependent";
constexpr std::string_view kMinuteOutOfMatch = "minute_out_of_match";
constexpr std::string_view kNoteText = "note_text";
constexpr std::string_view kReportNote = "report_note";
constexpr std::string_view kRating = "rating";
constexpr std::string_view kRpe = "rpe";
constexpr std::string_view kPlayerUnknown = "player_unknown";

// A match without Match Day and without minutes: 2 × 45.
constexpr int kDefaultFinalMinute = 90;

core::Error conflict(std::string_view field, std::string detail) {
    return core::Error{core::ErrorCode::Conflict, std::move(detail), std::string(field)};
}

core::Error invalid(std::string_view field, std::string detail) {
    return core::Error{core::ErrorCode::InvalidInput, std::move(detail), std::string(field)};
}

template <class T, class U> std::vector<T> draftsOf(const std::vector<U>& rows) {
    return {rows.begin(), rows.end()};
}

// The format of a match that Match Day did not run, from its final minute.
core::MatchFormat guessedFormat(int finalMinute) {
    if (finalMinute % 2 == 0) {
        const core::MatchFormat halves{2, finalMinute / 2};
        if (core::isValidMatchFormat(halves)) {
            return halves;
        }
    }
    const core::MatchFormat single{1, finalMinute};
    return core::isValidMatchFormat(single) ? single : core::MatchFormat{};
}

// The sheet of a match that Match Day did not run (P14 decision 2): the starters (`started`, or
// on from minute 0) with their position, the players who came on in that order, then the rest of
// the squad, who may come on in a correction.
core::MatchSheet rebuiltSheet(const std::vector<core::Appearance>& appearances,
                              const std::vector<core::Player>& squad) {
    core::MatchSheet sheet;
    std::vector<const core::Appearance*> cameOn;
    std::set<core::PlayerId> listed;
    for (const core::Appearance& appearance : appearances) {
        const auto player = std::ranges::find(squad, appearance.playerId, &core::Player::id);
        const core::Position fallback =
            player != squad.end() ? player->primaryPosition : core::Position::Cm;
        if (appearance.started || appearance.minuteOn == 0) {
            sheet.starters.push_back({appearance.playerId, appearance.position.value_or(fallback)});
        } else {
            cameOn.push_back(&appearance);
        }
        listed.insert(appearance.playerId);
    }
    std::ranges::stable_sort(cameOn, {}, [](const core::Appearance* appearance) {
        return appearance->minuteOn.value_or(core::kMaxMatchMinute);
    });
    for (const core::Appearance* appearance : cameOn) {
        sheet.bench.push_back(appearance->playerId);
    }
    for (const core::Player& player : squad) {
        if (!player.archivedAt && !listed.contains(player.id)) {
            sheet.bench.push_back(player.id);
        }
    }
    return sheet;
}

// The minutes and the score the events give are those stored.
bool sameAsStored(const core::MatchMinutes& minutes, const std::vector<core::Appearance>& stored,
                  const core::Match& match) {
    if (minutes.appearances.size() != stored.size() ||
        match.goalsFor != std::optional{minutes.goalsFor} ||
        match.goalsAgainst != std::optional{minutes.goalsAgainst}) {
        return false;
    }
    return std::ranges::all_of(minutes.appearances, [&](const core::AppearanceDraft& computed) {
        const auto old =
            std::ranges::find(stored, computed.playerId, &core::AppearanceDraft::playerId);
        return old != stored.end() && old->started == computed.started &&
               old->minuteOn == computed.minuteOn && old->minuteOff == computed.minuteOff;
    });
}

// std::ranges::contains is not in every standard library of the three platforms yet.
template <class Range, class Value, class Projection = std::identity>
bool containsValue(const Range& range, const Value& value, Projection projection = {}) {
    return std::ranges::find(range, value, projection) != std::ranges::end(range);
}

bool isSameTime(const core::MatchEvent& left, const core::MatchEvent& right) {
    return left.minute == right.minute && left.addedTime == right.addedTime;
}

} // namespace

struct MatchReportService::Basis {
    MatchReport report;
    std::optional<core::Lineup> lineup;
};

MatchReportService::MatchReportService(data::DatabaseManager& db, data::MatchRepository& matches,
                                       data::MatchLiveRepository& live,
                                       data::MatchNoteRepository& notes,
                                       data::MatchTacticRepository& tactics,
                                       data::LineupRepository& lineups,
                                       data::OpponentRepository& opponents,
                                       data::PlayerRepository& players, Session& session)
    : m_db(db), m_matches(matches), m_live(live), m_notes(notes), m_tactics(tactics),
      m_lineups(lineups), m_opponents(opponents), m_players(players), m_session(session) {}

core::Status MatchReportService::requireLogin() const {
    if (!m_session.user()) {
        return std::unexpected(core::Error{
            core::ErrorCode::PermissionDenied, "match report: nobody is logged in", {}});
    }
    return {};
}

core::Result<MatchReportService::Basis> MatchReportService::readBasis(core::MatchId id) const {
    auto match = m_matches.get(id);
    if (!match) {
        return std::unexpected(match.error());
    }
    const auto opponent = m_opponents.get(match->opponentId);
    if (!opponent) {
        return std::unexpected(opponent.error());
    }
    Basis basis;
    MatchReport& report = basis.report;
    report.match = CalendarMatch{*match, opponent->name};
    auto note = m_matches.reportNote(id);
    if (!note) {
        return std::unexpected(note.error());
    }
    report.reportNote = std::move(*note);
    auto appearances = m_matches.listAppearances(id);
    if (!appearances) {
        return std::unexpected(appearances.error());
    }
    auto storedEvents = m_matches.listEvents(id);
    if (!storedEvents) {
        return std::unexpected(storedEvents.error());
    }
    report.events = std::move(*storedEvents);
    auto notes = m_notes.list(id);
    if (!notes) {
        return std::unexpected(notes.error());
    }
    report.notes = std::move(*notes);
    auto changes = m_tactics.list(id);
    if (!changes) {
        return std::unexpected(changes.error());
    }
    report.tacticChanges = std::move(*changes);
    auto squad = m_players.list(match->teamId, data::ArchivedPlayers::Include);
    if (!squad) {
        return std::unexpected(squad.error());
    }
    const auto live = m_live.find(id);
    if (!live) {
        return std::unexpected(live.error());
    }
    auto lineup = m_lineups.findForMatch(id);
    if (!lineup) {
        return std::unexpected(lineup.error());
    }

    const auto events = draftsOf<core::MatchEventDraft>(report.events);
    const auto changeDrafts = draftsOf<core::MatchTacticChangeDraft>(report.tacticChanges);
    std::expected<core::MatchMinutes, core::MatchEventError> minutes;
    if (*live && *lineup) {
        report.fromMatchDay = true;
        report.format = (*live)->format;
        report.finalMinute = (*live)->endedEarlyAt.value_or(report.format.totalMinutes());
        report.sheet = core::matchSheetOf(**lineup);
        basis.lineup = **lineup;
        minutes = core::computeMatchMinutes(**lineup, events, changeDrafts, report.finalMinute);
        if (auto names = core::shapeChangeNames(**lineup, events, changeDrafts); names) {
            report.tacticChangeNames = std::move(*names);
        }
    } else {
        int last = 0;
        for (const core::Appearance& appearance : *appearances) {
            last = std::max(last, appearance.minuteOff.value_or(0));
        }
        report.finalMinute = last > 0 ? last : kDefaultFinalMinute;
        report.format = guessedFormat(report.finalMinute);
        report.sheet = rebuiltSheet(*appearances, *squad);
        minutes = core::computeMatchMinutes(report.sheet, events, report.finalMinute);
    }
    report.correctable = match->status == core::MatchStatus::Finished && minutes.has_value() &&
                         sameAsStored(*minutes, *appearances, *match);

    // The players of the sheet, also archived ones (only those who played of the rest of the
    // squad: the others are just candidates for a correction).
    std::set<core::PlayerId> shown;
    for (const core::StartingPlayer& starter : report.sheet.starters) {
        shown.insert(starter.player);
    }
    if (report.fromMatchDay) {
        shown.insert(report.sheet.bench.begin(), report.sheet.bench.end());
    }
    for (const core::Appearance& appearance : *appearances) {
        shown.insert(appearance.playerId);
    }
    for (core::Player& player : *squad) {
        if (shown.contains(player.id) || containsValue(report.sheet.bench, player.id)) {
            report.players.push_back(std::move(player));
        }
    }

    // The players who played: the starters in the order of the sheet, then by the minute on.
    std::map<core::PlayerId, int> order;
    for (const core::StartingPlayer& starter : report.sheet.starters) {
        order.emplace(starter.player, static_cast<int>(order.size()));
    }
    report.appearances = std::move(*appearances);
    std::ranges::stable_sort(report.appearances, [&](const core::Appearance& left,
                                                     const core::Appearance& right) {
        const auto rank = [&](const core::Appearance& appearance) {
            const auto found = order.find(appearance.playerId);
            return std::pair{found != order.end() ? found->second : static_cast<int>(order.size()),
                             appearance.minuteOn.value_or(core::kMaxMatchMinute + 1)};
        };
        return rank(left) < rank(right);
    });

    // The shapes with their minutes, split by the changes of shape.
    if (basis.lineup) {
        const auto start = core::shapeAt(*basis.lineup, events, changeDrafts, core::MatchTime{0});
        std::string current = start ? start->formation : basis.lineup->formation;
        core::MatchTime from{0};
        for (std::size_t index = 0; index < report.tacticChanges.size(); ++index) {
            const core::MatchTacticChange& change = report.tacticChanges[index];
            const core::MatchTime at{change.minute, change.addedTime};
            if (index < report.tacticChangeNames.size()) {
                current = report.tacticChangeNames[index].from;
            }
            report.shapes.push_back({current, from, at});
            from = at;
            current = index < report.tacticChangeNames.size() ? report.tacticChangeNames[index].to
                                                              : change.shape.formation;
        }
        report.shapes.push_back({current, from, std::nullopt});
    }
    return basis;
}

core::Result<MatchReport> MatchReportService::report(core::MatchId match) const {
    if (auto allowed = requireLogin(); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto basis = readBasis(match);
    if (!basis) {
        return std::unexpected(basis.error());
    }
    return std::move(basis->report);
}

core::Result<std::vector<PlayerMatchRow>>
MatchReportService::playerMatches(core::PlayerId player) const {
    if (auto allowed = requireLogin(); !allowed) {
        return std::unexpected(allowed.error());
    }
    const auto rows = m_matches.listPlayerAppearances(player);
    if (!rows) {
        return std::unexpected(rows.error());
    }
    std::vector<PlayerMatchRow> result;
    result.reserve(rows->size());
    for (const data::PlayerAppearanceRow& row : *rows) {
        PlayerMatchRow entry;
        entry.match = row.match.id;
        entry.date = row.match.date;
        entry.opponentName = row.opponentName;
        entry.venue = row.match.venue;
        entry.goalsFor = row.match.goalsFor.value_or(0);
        entry.goalsAgainst = row.match.goalsAgainst.value_or(0);
        if (row.appearance.minuteOn && row.appearance.minuteOff) {
            entry.minutes = std::max(0, *row.appearance.minuteOff - *row.appearance.minuteOn);
        }
        entry.rating = row.appearance.rating;
        entry.rpe = row.appearance.rpe;
        const auto events = m_matches.listEvents(row.match.id);
        if (!events) {
            return std::unexpected(events.error());
        }
        for (const core::MatchEvent& event : *events) {
            const bool own = event.playerId == player;
            switch (event.type) {
            case core::MatchEventType::Goal:
                entry.goals += own ? 1 : 0;
                entry.assists += event.relatedPlayerId == player ? 1 : 0;
                break;
            case core::MatchEventType::Assist:
                entry.assists += own ? 1 : 0;
                break;
            case core::MatchEventType::Yellow:
                entry.yellowCards += own ? 1 : 0;
                break;
            case core::MatchEventType::Red:
                entry.redCards += own ? 1 : 0;
                break;
            case core::MatchEventType::Sub:
            case core::MatchEventType::OpponentGoal:
                break;
            }
        }
        result.push_back(std::move(entry));
    }
    return result;
}

core::Result<MatchReportService::Basis> MatchReportService::readForChange(core::MatchId match,
                                                                          bool corrections) const {
    if (auto allowed = m_session.require(core::Permission::EditLineup); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto basis = readBasis(match);
    if (!basis) {
        return std::unexpected(basis.error());
    }
    if (basis->report.match.match.status != core::MatchStatus::Finished) {
        return std::unexpected(conflict(kMatchNotFinished, "match report: not finished"));
    }
    if (corrections && !basis->report.correctable) {
        return std::unexpected(
            conflict(kNotCorrectable, "match report: the events do not replay on the sheet"));
    }
    return basis;
}

core::Status MatchReportService::save(core::MatchId match, std::span<const RatingEntry> ratings,
                                      std::string_view note) {
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto basis = readForChange(match, false);
    if (!basis) {
        return std::unexpected(basis.error());
    }
    std::vector<data::AppearanceRating> rows;
    rows.reserve(ratings.size());
    for (const RatingEntry& entry : ratings) {
        if (entry.rating && (*entry.rating < 1 || *entry.rating > 10)) {
            return std::unexpected(invalid(kRating, "match report: rating outside 1-10"));
        }
        if (entry.rpe && (*entry.rpe < 0 || *entry.rpe > 10)) {
            return std::unexpected(invalid(kRpe, "match report: rpe outside 0-10"));
        }
        if (!containsValue(basis->report.appearances, entry.player,
                           &core::AppearanceDraft::playerId)) {
            return std::unexpected(
                invalid(kPlayerUnknown, "match report: rating of a player who did not play"));
        }
        rows.push_back({entry.player, entry.rating, entry.rpe});
    }
    std::string text = core::trimmed(note);
    if (core::characterCount(text) > static_cast<std::size_t>(kMaxReportNoteLength)) {
        return std::unexpected(invalid(kReportNote, "match report: note too long"));
    }
    if (auto rated = m_matches.setRatings(match, rows); !rated) {
        return rated;
    }
    const std::optional<std::string> stored =
        text.empty() ? std::nullopt : std::optional{std::move(text)};
    if (auto noted = m_matches.setReportNote(match, stored); !noted) {
        return noted;
    }
    return transaction->commit();
}

core::Status MatchReportService::checkTime(const Basis& basis, core::MatchTime time) const {
    if (!core::isValidMatchTime(basis.report.format, time) ||
        time.minute > basis.report.finalMinute) {
        return std::unexpected(
            invalid(kMinuteOutOfMatch, "match report: minute outside the match"));
    }
    return {};
}

core::Result<core::MatchSituation> MatchReportService::situationAt(core::MatchId match,
                                                                   core::MatchTime time) const {
    if (auto allowed = requireLogin(); !allowed) {
        return std::unexpected(allowed.error());
    }
    const auto basis = readBasis(match);
    if (!basis) {
        return std::unexpected(basis.error());
    }
    const auto events = draftsOf<core::MatchEventDraft>(basis->report.events);
    auto situation = core::situationAt(basis->report.sheet, events, time);
    if (!situation) {
        return std::unexpected(matchHistoryError(situation.error(), "match report situation"));
    }
    return std::move(*situation);
}

core::Result<std::optional<EventConflict>>
MatchReportService::checkEvent(core::MatchId match, const core::MatchEventDraft& event) const {
    if (auto allowed = requireLogin(); !allowed) {
        return std::unexpected(allowed.error());
    }
    const auto basis = readBasis(match);
    if (!basis) {
        return std::unexpected(basis.error());
    }
    std::optional<EventConflict> found;
    const auto rows = eventActionRows(basis->report.sheet, basis->report.events, event,
                                      basis->report.finalMinute, found);
    if (!rows && !found) {
        return std::unexpected(rows.error());
    }
    return found;
}

core::Status MatchReportService::recompute(const Basis& basis) {
    const core::MatchId match = basis.report.match.match.id;
    std::expected<core::MatchMinutes, core::Error> minutes;
    if (basis.lineup) {
        minutes = recomputeAppearances(m_matches, m_tactics, match, *basis.lineup,
                                       basis.report.finalMinute);
    } else {
        const auto events = m_matches.listEvents(match);
        if (!events) {
            return std::unexpected(events.error());
        }
        const auto drafts = draftsOf<core::MatchEventDraft>(*events);
        auto computed =
            core::computeMatchMinutes(basis.report.sheet, drafts, basis.report.finalMinute);
        if (!computed) {
            return std::unexpected(matchHistoryError(computed.error(), "match report minutes"));
        }
        minutes = storeMinutes(m_matches, match, std::move(*computed), true);
    }
    if (!minutes) {
        return std::unexpected(minutes.error());
    }
    return m_matches.setResult(match, minutes->goalsFor, minutes->goalsAgainst,
                               core::MatchStatus::Finished);
}

core::Result<std::vector<core::MatchEventId>>
MatchReportService::addEvent(core::MatchId match, const core::MatchEventDraft& event) {
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto basis = readForChange(match, true);
    if (!basis) {
        return std::unexpected(basis.error());
    }
    if (auto valid = checkTime(*basis, {event.minute, event.addedTime}); !valid) {
        return std::unexpected(valid.error());
    }
    std::optional<EventConflict> found;
    const auto rows = eventActionRows(basis->report.sheet, basis->report.events, event,
                                      basis->report.finalMinute, found);
    if (!rows) {
        return std::unexpected(rows.error());
    }
    std::vector<core::MatchEventId> ids;
    for (const core::MatchEventDraft& row : *rows) {
        const auto id = m_matches.appendEvent(match, row);
        if (!id) {
            return std::unexpected(id.error());
        }
        ids.push_back(*id);
    }
    if (auto recomputed = recompute(*basis); !recomputed) {
        return std::unexpected(recomputed.error());
    }
    if (auto committed = transaction->commit(); !committed) {
        return std::unexpected(committed.error());
    }
    return ids;
}

core::Result<std::vector<core::MatchEvent>>
MatchReportService::rowsToDelete(const Basis& basis, core::MatchEventId id,
                                 std::optional<core::MatchEvent>& dependent) const {
    const std::vector<core::MatchEvent>& events = basis.report.events;
    const auto target = std::ranges::find(events, id, &core::MatchEvent::id);
    if (target == events.end()) {
        return std::unexpected(
            core::Error{core::ErrorCode::NotFound, "match report: no such event", {}});
    }
    const auto drafts = draftsOf<core::MatchEventDraft>(events);
    const std::vector<bool> secondYellows = core::secondYellowReds(drafts);
    // A second yellow and its red go together: the red of a second yellow, and the yellow of the
    // player at the same time just before it.
    std::set<core::MatchEventId> removed{id};
    const auto order = core::replayOrder(drafts);
    for (std::size_t position = 0; position < order.size(); ++position) {
        const std::size_t index = order[position];
        if (!secondYellows[index]) {
            continue;
        }
        const core::MatchEvent& red = events[index];
        for (std::size_t back = position; back-- > 0;) {
            const core::MatchEvent& yellow = events[order[back]];
            if (yellow.type == core::MatchEventType::Yellow && yellow.playerId == red.playerId &&
                isSameTime(yellow, red)) {
                if (removed.contains(red.id) || removed.contains(yellow.id)) {
                    removed.insert(red.id);
                    removed.insert(yellow.id);
                }
                break;
            }
        }
    }
    std::vector<core::MatchEvent> rows;
    std::vector<core::MatchEvent> kept;
    for (const core::MatchEvent& event : events) {
        (removed.contains(event.id) ? rows : kept).push_back(event);
    }
    // What is left must replay, and every red card must keep its meaning (P14 decision 5).
    const auto keptDrafts = draftsOf<core::MatchEventDraft>(kept);
    const auto replayed =
        core::replayMatch(basis.report.sheet, keptDrafts, basis.report.finalMinute);
    if (!replayed) {
        dependent = kept[replayed.error().index];
        return rows;
    }
    const std::vector<bool> keptSecondYellows = core::secondYellowReds(keptDrafts);
    for (std::size_t index = 0; index < kept.size(); ++index) {
        const auto before = std::ranges::find(events, kept[index].id, &core::MatchEvent::id);
        const auto beforeIndex = static_cast<std::size_t>(before - events.begin());
        if (keptSecondYellows[index] != secondYellows[beforeIndex]) {
            dependent = kept[index];
            break;
        }
    }
    return rows;
}

core::Result<std::optional<core::MatchEvent>>
MatchReportService::checkDeleteEvent(core::MatchId match, core::MatchEventId event) const {
    if (auto allowed = requireLogin(); !allowed) {
        return std::unexpected(allowed.error());
    }
    const auto basis = readBasis(match);
    if (!basis) {
        return std::unexpected(basis.error());
    }
    std::optional<core::MatchEvent> dependent;
    if (auto rows = rowsToDelete(*basis, event, dependent); !rows) {
        return std::unexpected(rows.error());
    }
    return dependent;
}

core::Result<std::vector<core::MatchEvent>>
MatchReportService::deleteEvent(core::MatchId match, core::MatchEventId event) {
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto basis = readForChange(match, true);
    if (!basis) {
        return std::unexpected(basis.error());
    }
    std::optional<core::MatchEvent> dependent;
    auto rows = rowsToDelete(*basis, event, dependent);
    if (!rows) {
        return std::unexpected(rows.error());
    }
    if (dependent) {
        return std::unexpected(
            conflict(kEventDependent,
                     std::format("match report: event {} depends on it", dependent->id.value)));
    }
    for (const core::MatchEvent& row : *rows) {
        if (auto deleted = m_matches.deleteEvent(match, row.id); !deleted) {
            return std::unexpected(deleted.error());
        }
    }
    if (auto recomputed = recompute(*basis); !recomputed) {
        return std::unexpected(recomputed.error());
    }
    if (auto committed = transaction->commit(); !committed) {
        return std::unexpected(committed.error());
    }
    return std::move(*rows);
}

core::Status MatchReportService::restoreEvents(core::MatchId match,
                                               std::span<const core::MatchEvent> events) {
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto basis = readForChange(match, true);
    if (!basis) {
        return std::unexpected(basis.error());
    }
    std::vector<core::MatchEvent> all = basis->report.events;
    for (const core::MatchEvent& event : events) {
        if (event.matchId != match) {
            return std::unexpected(invalid("match", "match report: event of another match"));
        }
        if (auto restored = m_matches.restoreEvent(event); !restored) {
            return restored;
        }
        all.push_back(event);
    }
    // Other corrections since the delete may not fit it any more.
    std::ranges::sort(all, {}, &core::MatchEvent::id);
    const auto drafts = draftsOf<core::MatchEventDraft>(all);
    if (const auto replayed =
            core::replayMatch(basis->report.sheet, drafts, basis->report.finalMinute);
        !replayed) {
        return std::unexpected(conflict(
            "event_conflict", std::format("match report: restored events no longer fit ({})",
                                          eventProblemField(replayed.error().problem))));
    }
    if (auto recomputed = recompute(*basis); !recomputed) {
        return recomputed;
    }
    return transaction->commit();
}

core::Status MatchReportService::checkNote(const Basis& basis, std::optional<core::PlayerId> player,
                                           std::string_view text) const {
    const std::size_t length = core::characterCount(core::trimmed(text));
    if (length == 0 || length > static_cast<std::size_t>(core::kMaxNoteLength)) {
        return std::unexpected(invalid(kNoteText, "match note: empty or too long"));
    }
    if (player && !containsValue(basis.report.players, *player, &core::Player::id)) {
        return std::unexpected(invalid(kPlayerUnknown, "match note: player not in the match"));
    }
    return {};
}

core::Result<core::MatchNoteId> MatchReportService::addNote(core::MatchId match,
                                                            core::MatchTime time,
                                                            std::optional<core::PlayerId> player,
                                                            std::string_view text) {
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto basis = readForChange(match, false);
    if (!basis) {
        return std::unexpected(basis.error());
    }
    if (auto valid = checkTime(*basis, time); !valid) {
        return std::unexpected(valid.error());
    }
    if (auto checked = checkNote(*basis, player, text); !checked) {
        return std::unexpected(checked.error());
    }
    const core::MatchNoteDraft draft{time.minute, time.addedTime, player, core::trimmed(text)};
    const auto id = m_notes.add(match, draft, std::nullopt);
    if (!id) {
        return std::unexpected(id.error());
    }
    if (auto committed = transaction->commit(); !committed) {
        return std::unexpected(committed.error());
    }
    return *id;
}

core::Status MatchReportService::updateNote(core::MatchId match, core::MatchNoteId note,
                                            core::MatchTime time,
                                            std::optional<core::PlayerId> player,
                                            std::string_view text) {
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto basis = readForChange(match, false);
    if (!basis) {
        return std::unexpected(basis.error());
    }
    if (auto valid = checkTime(*basis, time); !valid) {
        return valid;
    }
    if (auto checked = checkNote(*basis, player, text); !checked) {
        return checked;
    }
    const core::MatchNoteDraft draft{time.minute, time.addedTime, player, core::trimmed(text)};
    if (auto updated = m_notes.update(match, note, draft); !updated) {
        return updated;
    }
    return transaction->commit();
}

core::Result<core::MatchNote> MatchReportService::deleteNote(core::MatchId match,
                                                             core::MatchNoteId note) {
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    if (const auto basis = readForChange(match, false); !basis) {
        return std::unexpected(basis.error());
    }
    auto removed = m_notes.remove(match, note);
    if (!removed) {
        return std::unexpected(removed.error());
    }
    if (auto committed = transaction->commit(); !committed) {
        return std::unexpected(committed.error());
    }
    return std::move(*removed);
}

core::Result<core::MatchNoteId> MatchReportService::restoreNote(const core::MatchNote& note) {
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    if (const auto basis = readForChange(note.matchId, false); !basis) {
        return std::unexpected(basis.error());
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

core::Result<core::MatchTacticChange>
MatchReportService::deleteTacticChange(core::MatchId match, core::MatchTacticChangeId change) {
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto basis = readForChange(match, true);
    if (!basis) {
        return std::unexpected(basis.error());
    }
    const auto found =
        std::ranges::find(basis->report.tacticChanges, change, &core::MatchTacticChange::id);
    if (found == basis->report.tacticChanges.end()) {
        return std::unexpected(
            core::Error{core::ErrorCode::NotFound, "match report: no such change of shape", {}});
    }
    const auto removed = m_tactics.deleteWithSeq(match, found->seq);
    if (!removed) {
        return std::unexpected(removed.error());
    }
    if (auto recomputed = recompute(*basis); !recomputed) {
        return std::unexpected(recomputed.error());
    }
    if (auto committed = transaction->commit(); !committed) {
        return std::unexpected(committed.error());
    }
    return *found;
}

core::Status MatchReportService::restoreTacticChange(const core::MatchTacticChange& change) {
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto basis = readForChange(change.matchId, true);
    if (!basis) {
        return std::unexpected(basis.error());
    }
    if (auto created = m_tactics.create(change.matchId, change, change.seq); !created) {
        return std::unexpected(created.error());
    }
    if (auto recomputed = recompute(*basis); !recomputed) {
        return recomputed;
    }
    return transaction->commit();
}

} // namespace fdss::services
