#include "services/DashboardService.hpp"

#include "core/domain/Permissions.hpp"
#include "data/repositories/AvailabilityRepository.hpp"
#include "data/repositories/MatchRepository.hpp"
#include "data/repositories/OpponentRepository.hpp"
#include "data/repositories/PlayerRepository.hpp"
#include "data/repositories/TrainingRepository.hpp"
#include "services/Clock.hpp"
#include "services/Session.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <ranges>

namespace fdss::services {

namespace {

int daysBetween(core::Date from, core::Date to) {
    using std::chrono::sys_days;
    return static_cast<int>((sys_days{to} - sys_days{from}).count());
}

} // namespace

DashboardService::DashboardService(data::MatchRepository& matches,
                                   data::OpponentRepository& opponents,
                                   data::PlayerRepository& players,
                                   data::AvailabilityRepository& availability,
                                   data::TrainingRepository& training, LineupService& lineups,
                                   Session& session, const Clock& clock)
    : m_matches(matches), m_opponents(opponents), m_players(players), m_availability(availability),
      m_training(training), m_lineups(lineups), m_session(session), m_clock(clock) {}

core::Result<DashboardOverview> DashboardService::overview(core::TeamId team,
                                                           int yellowCardThreshold) const {
    if (auto allowed = m_session.require(core::Permission::ViewAnalysis); !allowed) {
        return std::unexpected(allowed.error());
    }
    DashboardOverview result;
    result.today = m_clock.today();

    // By date, then kick-off time.
    const auto matches = m_matches.list(team, {});
    if (!matches) {
        return std::unexpected(matches.error());
    }
    const auto live = std::ranges::find(*matches, core::MatchStatus::Live, &core::Match::status);
    const auto next =
        live != matches->end()
            ? live
            : std::ranges::find_if(*matches, [&](const core::Match& match) {
                  return match.status == core::MatchStatus::Planned && match.date >= result.today;
              });
    if (next != matches->end()) {
        auto name = opponentName(next->opponentId);
        if (!name) {
            return std::unexpected(name.error());
        }
        NextMatchInfo info{CalendarMatch{*next, std::move(*name)},
                           daysBetween(result.today, next->date), std::nullopt};
        if (next->status == core::MatchStatus::Planned) {
            auto lineup = m_lineups.matchLineup(next->id, yellowCardThreshold);
            if (!lineup) {
                return std::unexpected(lineup.error());
            }
            info.lineup = std::move(*lineup);
        }
        result.nextMatch = std::move(info);
    }

    for (const core::Match& match : *matches | std::views::reverse) {
        if (std::ssize(result.lastMatches) == kFormMatches) {
            break;
        }
        if (match.status != core::MatchStatus::Finished) {
            continue;
        }
        auto name = opponentName(match.opponentId);
        if (!name) {
            return std::unexpected(name.error());
        }
        const auto appearances = m_matches.listAppearances(match.id);
        if (!appearances) {
            return std::unexpected(appearances.error());
        }
        FormMatch form{CalendarMatch{match, std::move(*name)}, std::nullopt};
        int sum = 0;
        int rated = 0;
        for (const core::Appearance& appearance : *appearances) {
            if (appearance.rating) {
                sum += *appearance.rating;
                ++rated;
            }
        }
        if (rated > 0) {
            form.averageRating = static_cast<double>(sum) / rated;
        }
        result.lastMatches.push_back(std::move(form));
    }

    auto unavailablePlayers = unavailable(team, result.today);
    if (!unavailablePlayers) {
        return std::unexpected(unavailablePlayers.error());
    }
    result.unavailable = std::move(*unavailablePlayers);

    auto sessions = m_training.list(team, core::DateRange{result.today, result.today});
    if (!sessions) {
        return std::unexpected(sessions.error());
    }
    result.todaySessions = std::move(*sessions);
    return result;
}

core::Result<std::vector<UnavailablePlayer>> DashboardService::unavailable(core::TeamId team,
                                                                           core::Date today) const {
    const auto entries = m_availability.listActiveOn(team, today);
    if (!entries) {
        return std::unexpected(entries.error());
    }
    // The enumeration lists the types from most to least important, as core::playerStatusOn().
    std::map<core::PlayerId, core::Availability> deciding;
    for (const core::Availability& entry : *entries) {
        const auto [found, inserted] = deciding.try_emplace(entry.playerId, entry);
        if (!inserted && entry.type < found->second.type) {
            found->second = entry;
        }
    }
    std::vector<UnavailablePlayer> result;
    if (deciding.empty()) {
        return result;
    }
    const auto players = m_players.list(team, data::ArchivedPlayers::Exclude);
    if (!players) {
        return std::unexpected(players.error());
    }
    const bool details = m_session.isAllowed(core::Permission::ViewAvailabilityDetails);
    for (const core::Player& player : *players) {
        const auto found = deciding.find(player.id);
        if (found == deciding.end()) {
            continue;
        }
        UnavailablePlayer row{player, found->second};
        if (!details) {
            row.entry.description.reset();
        }
        result.push_back(std::move(row));
    }
    return result;
}

core::Result<std::string> DashboardService::opponentName(core::OpponentId id) const {
    auto opponent = m_opponents.get(id);
    if (!opponent) {
        return std::unexpected(opponent.error());
    }
    return std::move(opponent->name);
}

} // namespace fdss::services
