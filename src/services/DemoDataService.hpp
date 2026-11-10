#pragma once

#include "core/domain/Ids.hpp"
#include "core/domain/StaffUser.hpp"
#include "core/util/Error.hpp"

#include <vector>

namespace fdss::core::demo {
struct DemoDataset;
} // namespace fdss::core::demo

namespace fdss::data {
class AvailabilityRepository;
class DatabaseManager;
class MatchRepository;
class OpponentRepository;
class PlayerRepository;
class StaffRepository;
class TrainingRepository;
} // namespace fdss::data

namespace fdss::services {

class AuthService;
class Clock;
class Session;

struct DemoSummary {
    int players = 0;
    int sessions = 0;
    int matches = 0;
};

// The demo club of the first-run wizard (packet P10): the core generator's half season, stored for
// a team that has no data yet. The club, the team and the Coach come from SetupService before.
class DemoDataService {
public:
    DemoDataService(data::DatabaseManager& db, data::PlayerRepository& players,
                    data::AvailabilityRepository& availability, data::OpponentRepository& opponents,
                    data::MatchRepository& matches, data::TrainingRepository& training,
                    data::StaffRepository& staff, const AuthService& auth, const Session& session,
                    const Clock& clock);

    // Generates from the fixed seed with today 12 weeks after the start and stores everything in
    // one Transaction, so on any error nothing of it is written. Needs ManageStaff (it adds the
    // demo Assistant and Physio). A team that already has players, matches or sessions: Conflict.
    [[nodiscard]] core::Result<DemoSummary> load(core::TeamId team);

private:
    [[nodiscard]] core::Status ensureEmpty(core::TeamId team) const;
    [[nodiscard]] core::Status store(core::TeamId team, const core::demo::DemoDataset& data,
                                     const std::vector<core::PinHash>& staffPins);

    data::DatabaseManager& m_db;
    data::PlayerRepository& m_players;
    data::AvailabilityRepository& m_availability;
    data::OpponentRepository& m_opponents;
    data::MatchRepository& m_matches;
    data::TrainingRepository& m_training;
    data::StaffRepository& m_staff;
    const AuthService& m_auth;
    const Session& m_session;
    const Clock& m_clock;
};

} // namespace fdss::services
