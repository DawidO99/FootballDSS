#pragma once

#include "core/domain/Player.hpp"
#include "core/util/Error.hpp"

#include <vector>

namespace fdss::data {

class DatabaseManager;

enum class ArchivedPlayers { Exclude, Include };

// Players are archived, never deleted. A shirt number already taken gives Conflict with
// field = "shirt_number".
class PlayerRepository {
public:
    explicit PlayerRepository(DatabaseManager& db) : m_db(db) {}

    [[nodiscard]] core::Result<std::vector<core::Player>> list(core::TeamId team,
                                                               ArchivedPlayers archived) const;
    [[nodiscard]] core::Result<core::Player> get(core::PlayerId id) const;
    [[nodiscard]] core::Result<core::PlayerId> create(core::TeamId team,
                                                      const core::PlayerDraft& draft);
    [[nodiscard]] core::Status update(core::PlayerId id, const core::PlayerDraft& draft);
    // An archived player keeps the first archive date.
    [[nodiscard]] core::Status archive(core::PlayerId id, core::Date date);
    [[nodiscard]] core::Status unarchive(core::PlayerId id);

private:
    DatabaseManager& m_db;
};

} // namespace fdss::data
