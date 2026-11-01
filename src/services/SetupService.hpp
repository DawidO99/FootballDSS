#pragma once

#include "core/domain/Club.hpp"
#include "core/domain/StaffUser.hpp"
#include "core/domain/Team.hpp"
#include "core/util/Error.hpp"

#include <string_view>

namespace fdss::data {
class ClubRepository;
class DatabaseManager;
class StaffRepository;
} // namespace fdss::data

namespace fdss::services {

class AuthService;
class Session;

struct ClubContext {
    core::Club club;
    core::Team team; // the first team by id; there is no team switcher yet
};

// The first run: an empty database becomes a club with a team and its Coach.
class SetupService {
public:
    SetupService(data::DatabaseManager& db, data::ClubRepository& clubs,
                 data::StaffRepository& staff, AuthService& auth, Session& session);

    // A club, at least one team and at least one active Coach, so a login is possible.
    [[nodiscard]] core::Result<bool> isSetupComplete() const;
    // No club or no team yet: NotFound.
    [[nodiscard]] core::Result<ClubContext> clubContext() const;
    // Everything in one Transaction, then the Coach is logged in. InvalidInput names the input in
    // `field`: club_name, team_name, season, coach_name or pin. A second setup: Conflict.
    [[nodiscard]] core::Result<core::StaffUser> completeSetup(const core::ClubDraft& club,
                                                              const core::TeamDraft& team,
                                                              std::string_view coachName,
                                                              std::string_view pin);

private:
    data::DatabaseManager& m_db;
    data::ClubRepository& m_clubs;
    data::StaffRepository& m_staff;
    AuthService& m_auth;
    Session& m_session;
};

} // namespace fdss::services
