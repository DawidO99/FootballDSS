#pragma once

#include "core/domain/Availability.hpp"
#include "core/domain/DateTypes.hpp"
#include "core/domain/Ids.hpp"
#include "core/domain/Match.hpp"
#include "core/domain/Player.hpp"
#include "core/domain/TrainingSession.hpp"
#include "core/util/Error.hpp"
#include "services/LineupService.hpp"

#include <optional>
#include <string>
#include <vector>

namespace fdss::data {
class AvailabilityRepository;
class MatchRepository;
class OpponentRepository;
class PlayerRepository;
class TrainingRepository;
} // namespace fdss::data

namespace fdss::services {

class Clock;
class Session;

struct NextMatchInfo {
    CalendarMatch match;
    // 0 on the day of the match; a live match counts from its date, so it can be below 0.
    int daysToGo = 0;
    // Planned matches only: the lineup as Pre-match reads it (without load statuses).
    std::optional<MatchLineup> lineup;
};

struct UnavailablePlayer {
    core::Player player;
    // The entry that decides the status (core::playerStatusOn). Its description is empty for a
    // role without ViewAvailabilityDetails.
    core::Availability entry;
};

struct FormMatch {
    CalendarMatch match;
    // The mean of the stored ratings of the match; nullopt without any.
    std::optional<double> averageRating;
};

struct DashboardOverview {
    core::Date today{};
    // The live match, otherwise the first planned match on or after today.
    std::optional<NextMatchInfo> nextMatch;
    // Players out today, by surname as the repository sorts the squad.
    std::vector<UnavailablePlayer> unavailable;
    // The last finished matches, the most recent first, at most kFormMatches.
    std::vector<FormMatch> lastMatches;
    // Sessions today, by start time.
    std::vector<core::TrainingSession> todaySessions;
};

// The first screen after login (SPEC section 4, packet P15): the next match, who is out, recent
// form and today's sessions. Load signals come from LoadService. Needs ViewAnalysis, which every
// role has, and writes nothing. A few small queries on the calling thread.
class DashboardService {
public:
    static constexpr int kFormMatches = 5;

    DashboardService(data::MatchRepository& matches, data::OpponentRepository& opponents,
                     data::PlayerRepository& players, data::AvailabilityRepository& availability,
                     data::TrainingRepository& training, LineupService& lineups, Session& session,
                     const Clock& clock);

    [[nodiscard]] core::Result<DashboardOverview> overview(core::TeamId team,
                                                           int yellowCardThreshold) const;

private:
    [[nodiscard]] core::Result<std::vector<UnavailablePlayer>> unavailable(core::TeamId team,
                                                                           core::Date today) const;
    [[nodiscard]] core::Result<std::string> opponentName(core::OpponentId id) const;

    data::MatchRepository& m_matches;
    data::OpponentRepository& m_opponents;
    data::PlayerRepository& m_players;
    data::AvailabilityRepository& m_availability;
    data::TrainingRepository& m_training;
    LineupService& m_lineups;
    Session& m_session;
    const Clock& m_clock;
};

} // namespace fdss::services
