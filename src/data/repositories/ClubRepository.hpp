#pragma once

#include "core/domain/Club.hpp"
#include "core/domain/Team.hpp"
#include "core/util/Error.hpp"

#include <vector>

namespace fdss::data {

class DatabaseManager;

class ClubRepository {
public:
    explicit ClubRepository(DatabaseManager& db) : m_db(db) {}

    // One file holds one club. No club yet: NotFound.
    [[nodiscard]] core::Result<core::Club> getClub() const;
    // A second club: Conflict. The schema does not enforce this.
    [[nodiscard]] core::Result<core::ClubId> createClub(const core::ClubDraft& draft);

    [[nodiscard]] core::Result<std::vector<core::Team>> listTeams(core::ClubId club) const;
    [[nodiscard]] core::Result<core::TeamId> createTeam(core::ClubId club,
                                                        const core::TeamDraft& draft);
    [[nodiscard]] core::Status updateTeam(core::TeamId team, const core::TeamDraft& draft);

private:
    DatabaseManager& m_db;
};

} // namespace fdss::data
