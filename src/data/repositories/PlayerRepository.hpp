#pragma once

#include "core/domain/Player.hpp"
#include "core/util/Error.hpp"

#include <optional>
#include <vector>

namespace fdss::data {

class DatabaseManager;

enum class ArchivedPlayers { Exclude, Include };

// Players are archived, never deleted, except by undoing an import (remove(), packet P16
// decision 3). A shirt number already taken gives Conflict with field = "shirt_number".
class PlayerRepository {
public:
    explicit PlayerRepository(DatabaseManager& db) : m_db(db) {}

    [[nodiscard]] core::Result<std::vector<core::Player>> list(core::TeamId team,
                                                               ArchivedPlayers archived) const;
    [[nodiscard]] core::Result<core::Player> get(core::PlayerId id) const;
    // `importId`: the import that creates the player (undo of the import).
    [[nodiscard]] core::Result<core::PlayerId>
    create(core::TeamId team, const core::PlayerDraft& draft,
           std::optional<core::ImportId> importId = std::nullopt);
    [[nodiscard]] core::Status update(core::PlayerId id, const core::PlayerDraft& draft);
    // An archived player keeps the first archive date.
    [[nodiscard]] core::Status archive(core::PlayerId id, core::Date date);
    [[nodiscard]] core::Status unarchive(core::PlayerId id);

    // The players an import created, archived ones included, in id order.
    [[nodiscard]] core::Result<std::vector<core::Player>> listByImport(core::ImportId id) const;
    // Whether a saved lineup (`lineup.tactic_json`, no foreign key) has the player in a slot or on
    // the bench.
    [[nodiscard]] core::Result<bool> isInLineup(core::PlayerId id) const;
    // Only for undoing an import. A row that references the player: Conflict.
    [[nodiscard]] core::Status remove(core::PlayerId id);

private:
    DatabaseManager& m_db;
};

} // namespace fdss::data
