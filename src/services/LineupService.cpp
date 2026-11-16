#include "services/LineupService.hpp"

#include "core/algorithms/Formations.hpp"
#include "core/domain/Availability.hpp"
#include "core/domain/Permissions.hpp"
#include "core/domain/Validation.hpp"
#include "data/db/Transaction.hpp"
#include "data/readers/TacticJson.hpp"
#include "data/repositories/AvailabilityRepository.hpp"
#include "data/repositories/LineupRepository.hpp"
#include "data/repositories/MatchRepository.hpp"
#include "data/repositories/OpponentRepository.hpp"
#include "data/repositories/PlayerRepository.hpp"
#include "data/repositories/TrainingRepository.hpp"
#include "services/Clock.hpp"
#include "services/Session.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

namespace fdss::services {

namespace {

constexpr std::string_view kMatchNotPlanned = "match_not_planned";
constexpr std::string_view kTemplateName = "template_name";
constexpr std::string_view kLineupShape = "lineup_shape";
constexpr std::string_view kDefaultFormation = "4-4-2";

core::Error shapeError(std::string detail) {
    return core::Error{core::ErrorCode::InvalidInput, "lineup: " + std::move(detail),
                       std::string(kLineupShape)};
}

// What a saved lineup must be, whatever the board did.
core::Status checkShape(const core::Lineup& lineup) {
    if (lineup.pitchSlots.size() != core::kLineupSlots) {
        return std::unexpected(shapeError("not 11 slots"));
    }
    if (std::ranges::count(lineup.pitchSlots, core::Position::Gk, &core::LineupSlot::position) >
        1) {
        return std::unexpected(shapeError("more than one goalkeeper slot"));
    }
    const auto inside = [](double value) {
        return std::isfinite(value) && value >= 0.0 && value <= 1.0;
    };
    if (!std::ranges::all_of(lineup.pitchSlots, [&](const core::LineupSlot& slot) {
            return inside(slot.x) && inside(slot.y);
        })) {
        return std::unexpected(shapeError("coordinates outside 0-1"));
    }
    if (std::ssize(lineup.bench) > core::kMaxBench) {
        return std::unexpected(shapeError("more than 12 on the bench"));
    }
    return {};
}

// The lineup as it is stored: its name counted from the slots.
core::Lineup named(core::Lineup lineup) {
    lineup.formation = core::formationName(lineup.pitchSlots);
    return lineup;
}

QString foldedName(std::string_view name) {
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size())).toCaseFolded();
}

} // namespace

int dropOutsiders(core::Lineup& lineup, const std::set<core::PlayerId>& squad) {
    int dropped = 0;
    for (core::LineupSlot& slot : lineup.pitchSlots) {
        if (slot.player && !squad.contains(*slot.player)) {
            slot.player.reset();
            ++dropped;
        }
    }
    const auto removed = std::ranges::remove_if(
        lineup.bench, [&](core::PlayerId id) { return !squad.contains(id); });
    dropped += static_cast<int>(removed.size());
    lineup.bench.erase(removed.begin(), removed.end());
    return dropped;
}

LineupService::LineupService(data::DatabaseManager& db, data::LineupRepository& lineups,
                             data::MatchRepository& matches, data::OpponentRepository& opponents,
                             data::PlayerRepository& players,
                             data::AvailabilityRepository& availability,
                             data::TrainingRepository& training, Session& session,
                             const Clock& clock)
    : m_db(db), m_lineups(lineups), m_matches(matches), m_opponents(opponents), m_players(players),
      m_availability(availability), m_training(training), m_session(session), m_clock(clock) {}

core::Result<std::vector<CalendarMatch>> LineupService::plannedMatches(core::TeamId team) const {
    if (auto allowed = requireLogin(); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto matches = m_matches.list(team, {});
    if (!matches) {
        return std::unexpected(matches.error());
    }
    const auto opponents = m_opponents.list();
    if (!opponents) {
        return std::unexpected(opponents.error());
    }
    std::map<core::OpponentId, std::string> names;
    for (const core::Opponent& opponent : *opponents) {
        names.emplace(opponent.id, opponent.name);
    }
    std::vector<CalendarMatch> planned;
    for (core::Match& match : *matches) {
        if (match.status == core::MatchStatus::Planned) {
            std::string name = names[match.opponentId];
            planned.push_back(CalendarMatch{std::move(match), std::move(name)});
        }
    }
    return planned;
}

core::Result<std::optional<core::MatchId>>
LineupService::nextPlannedMatch(core::TeamId team) const {
    const auto planned = plannedMatches(team);
    if (!planned) {
        return std::unexpected(planned.error());
    }
    if (planned->empty()) {
        return std::optional<core::MatchId>{};
    }
    const core::Date today = m_clock.today();
    const auto upcoming = std::ranges::find_if(
        *planned, [&](const CalendarMatch& item) { return item.match.date >= today; });
    return std::optional<core::MatchId>(upcoming != planned->end() ? upcoming->match.id
                                                                   : planned->back().match.id);
}

core::Result<MatchLineup> LineupService::matchLineup(core::MatchId matchId,
                                                     int yellowCardThreshold) const {
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
    MatchLineup result;
    result.match = CalendarMatch{*match, opponent->name};

    auto earlier = m_matches.list(match->teamId, {std::nullopt, match->date});
    if (!earlier) {
        return std::unexpected(earlier.error());
    }
    for (const core::Match& other : *earlier) {
        if (other.status == core::MatchStatus::Finished && other.opponentId == match->opponentId &&
            other.date < match->date) {
            result.lastMeeting = other; // by date, so the last one stays
        }
    }

    auto players = m_players.list(match->teamId, data::ArchivedPlayers::Exclude);
    if (!players) {
        return std::unexpected(players.error());
    }
    const auto active = m_availability.listActiveOn(match->teamId, match->date);
    if (!active) {
        return std::unexpected(active.error());
    }
    const auto missed = missedRecentSessions(match->teamId, match->date);
    if (!missed) {
        return std::unexpected(missed.error());
    }
    const auto yellows = yellowCards(*match);
    if (!yellows) {
        return std::unexpected(yellows.error());
    }
    result.context.yellowCardThreshold = yellowCardThreshold;
    std::set<core::PlayerId> squad;
    for (core::Player& player : *players) {
        std::vector<core::Availability> entries;
        std::ranges::copy_if(
            *active, std::back_inserter(entries),
            [&](const core::Availability& entry) { return entry.playerId == player.id; });
        core::LineupPlayerFacts facts;
        facts.primaryPosition = player.primaryPosition;
        facts.secondaryPositions = player.secondaryPositions;
        facts.statusOnMatchDay = core::playerStatusOn(entries, match->date);
        facts.missedRecentSessions = missed->contains(player.id);
        const auto cards = yellows->find(player.id);
        facts.yellowCards = cards == yellows->end() ? 0 : cards->second;
        result.context.players.emplace(player.id, facts);
        squad.insert(player.id);
        result.candidates.push_back(LineupCandidate{std::move(player), std::move(facts)});
    }

    const auto saved = m_lineups.findForMatch(matchId);
    if (!saved) {
        return std::unexpected(saved.error());
    }
    if (*saved) {
        result.lineup = **saved;
        result.saved = true;
        result.droppedPlayers = dropOutsiders(result.lineup, squad);
    } else {
        const core::Formation* formation =
            match->formation ? core::findFormation(*match->formation) : nullptr;
        result.lineup =
            core::emptyLineup(formation ? *formation : *core::findFormation(kDefaultFormation));
    }
    return result;
}

std::vector<core::LineupIssue> LineupService::check(const core::Lineup& lineup,
                                                    const core::LineupContext& context) const {
    return m_validator.validate(lineup, context);
}

core::Result<std::vector<LineupTemplateInfo>> LineupService::templates() const {
    if (auto allowed = requireLogin(); !allowed) {
        return std::unexpected(allowed.error());
    }
    const auto rows = m_lineups.listTemplates();
    if (!rows) {
        return std::unexpected(rows.error());
    }
    std::vector<LineupTemplateInfo> result;
    result.reserve(rows->size());
    for (const data::LineupTemplateRow& row : *rows) {
        result.push_back(LineupTemplateInfo{row.id, row.name, row.formation});
    }
    return result;
}

core::Status LineupService::exportTactic(const QString& path, const core::Lineup& lineup) const {
    if (auto allowed = requireLogin(); !allowed) {
        return allowed;
    }
    if (auto shape = checkShape(lineup); !shape) {
        return shape;
    }
    return data::writeTacticFile(path, named(lineup));
}

core::Status LineupService::save(core::MatchId match, const core::Lineup& lineup) {
    if (auto allowed = m_session.require(core::Permission::EditLineup); !allowed) {
        return allowed;
    }
    if (auto shape = checkShape(lineup); !shape) {
        return shape;
    }
    // The old row goes before the new one is written: both in one transaction (Codex review).
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    auto saved = m_lineups.saveForMatch(match, named(lineup));
    if (!saved) {
        if (saved.error().code == core::ErrorCode::Conflict) {
            saved.error().field = kMatchNotPlanned;
        }
        return saved;
    }
    return transaction->commit();
}

core::Result<LoadedLineup> LineupService::loadTemplate(core::LineupId id, core::TeamId team) const {
    if (auto allowed = m_session.require(core::Permission::EditLineup); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto lineup = m_lineups.getTemplate(id);
    if (!lineup) {
        return std::unexpected(lineup.error());
    }
    const auto squad = squadOf(team);
    if (!squad) {
        return std::unexpected(squad.error());
    }
    LoadedLineup loaded{std::move(*lineup), 0};
    loaded.droppedPlayers = dropOutsiders(loaded.lineup, *squad);
    return loaded;
}

core::Result<core::LineupId> LineupService::saveTemplate(std::string_view name,
                                                         const core::Lineup& lineup, bool replace) {
    if (auto allowed = m_session.require(core::Permission::EditLineup); !allowed) {
        return std::unexpected(allowed.error());
    }
    const std::string trimmedName = core::trimmed(name);
    const std::size_t length = core::characterCount(trimmedName);
    if (length == 0 || length > kMaxTemplateNameLength) {
        return std::unexpected(core::Error{core::ErrorCode::InvalidInput,
                                           "template name: empty or too long",
                                           std::string(kTemplateName)});
    }
    if (auto shape = checkShape(lineup); !shape) {
        return std::unexpected(shape.error());
    }
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto existing = findTemplate(trimmedName);
    if (!existing) {
        return std::unexpected(existing.error());
    }
    core::LineupId id;
    if (*existing) {
        if (!replace) {
            return std::unexpected(core::Error{core::ErrorCode::Conflict,
                                               "template name: already taken",
                                               std::string(kTemplateName)});
        }
        if (auto replaced = m_lineups.replaceTemplate(**existing, named(lineup)); !replaced) {
            return std::unexpected(replaced.error());
        }
        id = **existing;
    } else {
        const auto created = m_lineups.createTemplate(trimmedName, named(lineup));
        if (!created) {
            return std::unexpected(created.error());
        }
        id = *created;
    }
    if (auto committed = transaction->commit(); !committed) {
        return std::unexpected(committed.error());
    }
    return id;
}

core::Result<DeletedTemplate> LineupService::deleteTemplate(core::LineupId id) {
    if (auto allowed = m_session.require(core::Permission::EditLineup); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto rows = m_lineups.listTemplates();
    if (!rows) {
        return std::unexpected(rows.error());
    }
    const auto row = std::ranges::find(*rows, id, &data::LineupTemplateRow::id);
    if (row == rows->end()) {
        return std::unexpected(
            core::Error{core::ErrorCode::NotFound, "delete lineup template: no such template", {}});
    }
    auto lineup = m_lineups.getTemplate(id);
    if (!lineup) {
        return std::unexpected(lineup.error());
    }
    if (auto removed = m_lineups.removeTemplate(id); !removed) {
        return std::unexpected(removed.error());
    }
    if (auto committed = transaction->commit(); !committed) {
        return std::unexpected(committed.error());
    }
    return DeletedTemplate{row->name, std::move(*lineup)};
}

core::Result<core::LineupId> LineupService::restoreTemplate(const DeletedTemplate& deleted) {
    return saveTemplate(deleted.name, deleted.lineup, false);
}

core::Result<LoadedLineup> LineupService::importTactic(const QString& path,
                                                       core::TeamId team) const {
    if (auto allowed = m_session.require(core::Permission::EditLineup); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto lineup = data::readTacticFile(path);
    if (!lineup) {
        return std::unexpected(lineup.error());
    }
    const auto squad = squadOf(team);
    if (!squad) {
        return std::unexpected(squad.error());
    }
    LoadedLineup loaded{std::move(*lineup), 0};
    loaded.droppedPlayers = dropOutsiders(loaded.lineup, *squad);
    return loaded;
}

core::Status LineupService::requireLogin() const {
    if (m_session.user()) {
        return {};
    }
    return std::unexpected(
        core::Error{core::ErrorCode::PermissionDenied, "nobody is logged in", {}});
}

core::Result<std::set<core::PlayerId>> LineupService::squadOf(core::TeamId team) const {
    const auto players = m_players.list(team, data::ArchivedPlayers::Exclude);
    if (!players) {
        return std::unexpected(players.error());
    }
    std::set<core::PlayerId> squad;
    for (const core::Player& player : *players) {
        squad.insert(player.id);
    }
    return squad;
}

core::Result<std::optional<core::LineupId>>
LineupService::findTemplate(std::string_view name) const {
    const auto rows = m_lineups.listTemplates();
    if (!rows) {
        return std::unexpected(rows.error());
    }
    const QString folded = foldedName(name);
    for (const data::LineupTemplateRow& row : *rows) {
        if (foldedName(row.name) == folded) {
            return std::optional<core::LineupId>(row.id);
        }
    }
    return std::optional<core::LineupId>{};
}

core::Result<std::set<core::PlayerId>>
LineupService::missedRecentSessions(core::TeamId team, core::Date before) const {
    const core::Date dayBefore{std::chrono::sys_days{before} - std::chrono::days{1}};
    const core::Date last = std::min(dayBefore, m_clock.today());
    const auto sessions = m_training.list(team, {std::nullopt, last});
    if (!sessions) {
        return std::unexpected(sessions.error());
    }
    // The newest sessions with saved attendance, newest first.
    std::vector<std::vector<core::Attendance>> recent;
    for (auto session = sessions->rbegin();
         session != sessions->rend() && std::ssize(recent) < core::kRecentSessions; ++session) {
        auto attendance = m_training.listAttendance(session->id);
        if (!attendance) {
            return std::unexpected(attendance.error());
        }
        if (!attendance->empty()) {
            recent.push_back(std::move(*attendance));
        }
    }
    std::set<core::PlayerId> missed;
    if (std::ssize(recent) < core::kRecentSessions) {
        return missed;
    }
    // A player missed them all when every session has a row for them that is not present.
    std::map<core::PlayerId, int> notPresent;
    for (const auto& attendance : recent) {
        for (const core::Attendance& row : attendance) {
            if (row.status != core::AttendanceStatus::Present) {
                ++notPresent[row.playerId];
            }
        }
    }
    for (const auto& [player, count] : notPresent) {
        if (count == core::kRecentSessions) {
            missed.insert(player);
        }
    }
    return missed;
}

core::Result<std::map<core::PlayerId, int>>
LineupService::yellowCards(const core::Match& match) const {
    const auto matches = m_matches.list(match.teamId, {std::nullopt, match.date});
    if (!matches) {
        return std::unexpected(matches.error());
    }
    std::map<core::PlayerId, int> cards;
    for (const core::Match& other : *matches) {
        if (other.id == match.id || other.date >= match.date ||
            other.status == core::MatchStatus::Planned || other.competition != match.competition) {
            continue;
        }
        const auto events = m_matches.listEvents(other.id);
        if (!events) {
            return std::unexpected(events.error());
        }
        std::map<core::PlayerId, int> yellows;
        std::set<core::PlayerId> sentOff;
        for (const core::MatchEvent& event : *events) {
            if (!event.playerId) {
                continue;
            }
            if (event.type == core::MatchEventType::Yellow) {
                ++yellows[*event.playerId];
            } else if (event.type == core::MatchEventType::Red) {
                sentOff.insert(*event.playerId);
            }
        }
        for (const auto& [player, count] : yellows) {
            if (!sentOff.contains(player)) {
                cards[player] += count;
            }
        }
    }
    return cards;
}

} // namespace fdss::services
