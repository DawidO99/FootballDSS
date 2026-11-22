#pragma once

#include "core/domain/Availability.hpp"
#include "core/domain/Player.hpp"
#include "core/util/Error.hpp"

#include <utility>
#include <vector>

namespace fdss::data {
class AvailabilityRepository;
class PlayerRepository;
} // namespace fdss::data

namespace fdss::services {

class Clock;
class Session;

struct SquadPlayer {
    core::Player player;
    // For today. Means nothing for an archived player: the availability of those is not read.
    core::PlayerStatus status = core::PlayerStatus::Available;
};

// What SquadService::deleteAvailability() took away, for its undo. It holds the description also
// for a role that may not read it; only SquadService looks inside (P19).
class DeletedAvailability {
public:
    [[nodiscard]] core::PlayerId player() const { return m_entry.playerId; }

private:
    friend class SquadService;
    explicit DeletedAvailability(core::Availability entry) : m_entry(std::move(entry)) {}

    core::Availability m_entry;
};

// Players and their availability, with the role checks of SPEC section 5. Reading needs a logged-in
// user of any role, otherwise PermissionDenied. A description never leaves this class for a role
// without ViewAvailabilityDetails.
class SquadService {
public:
    SquadService(data::PlayerRepository& players, data::AvailabilityRepository& availability,
                 Session& session, const Clock& clock);

    // Archived players included.
    [[nodiscard]] core::Result<std::vector<SquadPlayer>> listPlayers(core::TeamId team) const;
    [[nodiscard]] core::Result<SquadPlayer> player(core::PlayerId id) const;
    // Newest first. The description is nullopt without ViewAvailabilityDetails.
    [[nodiscard]] core::Result<std::vector<core::Availability>>
    listAvailability(core::PlayerId player) const;

    // The methods below need EditSquad. A shirt number already taken: Conflict with field
    // "shirt_number", from unarchivePlayer too.
    [[nodiscard]] core::Result<core::PlayerId> addPlayer(core::TeamId team,
                                                         const core::PlayerDraft& draft);
    [[nodiscard]] core::Status updatePlayer(core::PlayerId id, const core::PlayerDraft& draft);
    [[nodiscard]] core::Status archivePlayer(core::PlayerId id);
    [[nodiscard]] core::Status unarchivePlayer(core::PlayerId id);

    // The methods below need EditAvailability. A description set by a role without
    // ViewAvailabilityDetails gives PermissionDenied with field "description"; such a role passes
    // nullopt, which keeps the stored text on update.
    [[nodiscard]] core::Result<core::AvailabilityId>
    addAvailability(core::PlayerId player, const core::AvailabilityDraft& draft);
    [[nodiscard]] core::Status updateAvailability(core::AvailabilityId id,
                                                  const core::AvailabilityDraft& draft);
    // actualTo is the first available day. Before the start of the entry: InvalidInput with field
    // "actual_to". An entry that is not the player's: NotFound.
    [[nodiscard]] core::Status markReturned(core::PlayerId player, core::AvailabilityId id,
                                            core::Date actualTo);
    // An entry made by mistake. NotFound when there is no such entry.
    [[nodiscard]] core::Result<DeletedAvailability> deleteAvailability(core::AvailabilityId id);
    // Brings the entry back with its description, under a new id.
    [[nodiscard]] core::Result<core::AvailabilityId>
    restoreAvailability(const DeletedAvailability& deleted);

private:
    [[nodiscard]] core::Status requireLogin() const;
    [[nodiscard]] core::Status checkDescription(const core::AvailabilityDraft& draft) const;
    [[nodiscard]] int currentYear() const;

    data::PlayerRepository& m_players;
    data::AvailabilityRepository& m_availability;
    Session& m_session;
    const Clock& m_clock;
};

} // namespace fdss::services
