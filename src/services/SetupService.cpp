#include "services/SetupService.hpp"

#include "core/domain/Validation.hpp"
#include "data/db/DatabaseManager.hpp"
#include "data/db/Transaction.hpp"
#include "data/repositories/ClubRepository.hpp"
#include "data/repositories/StaffRepository.hpp"
#include "services/AuthService.hpp"
#include "services/ServiceSupport.hpp"
#include "services/Session.hpp"

#include <string>
#include <utility>

namespace fdss::services {

namespace {

// validate() reports "name" for the club, the team and the coach alike; the wizard has to tell them
// apart.
core::Status withNameField(core::Status status, std::string_view nameField) {
    if (!status && status.error().field == "name") {
        status.error().field = nameField;
    }
    return status;
}

} // namespace

SetupService::SetupService(data::DatabaseManager& db, data::ClubRepository& clubs,
                           data::StaffRepository& staff, AuthService& auth, Session& session)
    : m_db(db), m_clubs(clubs), m_staff(staff), m_auth(auth), m_session(session) {}

core::Result<bool> SetupService::isSetupComplete() const {
    const auto club = m_clubs.getClub();
    if (!club) {
        if (club.error().code == core::ErrorCode::NotFound) {
            return false;
        }
        return std::unexpected(club.error());
    }
    const auto teams = m_clubs.listTeams(club->id);
    if (!teams) {
        return std::unexpected(teams.error());
    }
    const auto coaches = m_staff.countActiveCoaches();
    if (!coaches) {
        return std::unexpected(coaches.error());
    }
    return !teams->empty() && *coaches > 0;
}

core::Result<ClubContext> SetupService::clubContext() const {
    auto club = m_clubs.getClub();
    if (!club) {
        return std::unexpected(club.error());
    }
    auto teams = m_clubs.listTeams(club->id);
    if (!teams) {
        return std::unexpected(teams.error());
    }
    if (teams->empty()) {
        return std::unexpected(
            core::Error{core::ErrorCode::NotFound, "the club has no team yet", {}});
    }
    return ClubContext{std::move(*club), std::move(teams->front())};
}

core::Result<core::StaffUser> SetupService::completeSetup(const core::ClubDraft& club,
                                                          const core::TeamDraft& team,
                                                          std::string_view coachName,
                                                          std::string_view pin) {
    core::StaffUserDraft coach;
    coach.name = std::string(coachName);
    coach.role = core::Role::Coach;

    if (auto valid = withNameField(firstError(core::validate(club)), "club_name"); !valid) {
        return std::unexpected(valid.error());
    }
    if (auto valid = withNameField(firstError(core::validate(team)), "team_name"); !valid) {
        return std::unexpected(valid.error());
    }
    if (auto valid = withNameField(firstError(core::validate(coach)), "coach_name"); !valid) {
        return std::unexpected(valid.error());
    }
    // Before the transaction: the write lock is not held for the length of a hash.
    const auto hash = m_auth.hashPin(pin);
    if (!hash) {
        return std::unexpected(hash.error());
    }

    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto clubId = m_clubs.createClub(club);
    if (!clubId) {
        return std::unexpected(clubId.error());
    }
    if (const auto teamId = m_clubs.createTeam(*clubId, team); !teamId) {
        return std::unexpected(teamId.error());
    }
    const auto coachId = m_staff.create(coach, *hash);
    if (!coachId) {
        return std::unexpected(coachId.error());
    }
    auto user = m_staff.get(*coachId);
    if (!user) {
        return std::unexpected(user.error());
    }
    if (auto status = transaction->commit(); !status) {
        return std::unexpected(status.error());
    }
    m_session.start(*user);
    return user;
}

core::Status SetupService::updateTeam(std::string_view name, std::string_view season) {
    if (auto allowed = m_session.require(core::Permission::ManageSettings); !allowed) {
        return allowed;
    }
    const auto context = clubContext();
    if (!context) {
        return std::unexpected(context.error());
    }
    core::TeamDraft team = context->team;
    team.name = core::trimmed(name);
    team.season = core::trimmed(season);
    if (auto valid = withNameField(firstError(core::validate(team)), "team_name"); !valid) {
        return valid;
    }
    return m_clubs.updateTeam(context->team.id, team);
}

} // namespace fdss::services
