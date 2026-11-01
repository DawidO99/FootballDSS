#include "services/SquadService.hpp"

#include "core/domain/Permissions.hpp"
#include "core/domain/Validation.hpp"
#include "data/repositories/AvailabilityRepository.hpp"
#include "data/repositories/PlayerRepository.hpp"
#include "services/Clock.hpp"
#include "services/ServiceSupport.hpp"
#include "services/Session.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace fdss::services {

SquadService::SquadService(data::PlayerRepository& players,
                           data::AvailabilityRepository& availability, Session& session,
                           const Clock& clock)
    : m_players(players), m_availability(availability), m_session(session), m_clock(clock) {}

core::Result<std::vector<SquadPlayer>> SquadService::listPlayers(core::TeamId team) const {
    if (auto allowed = requireLogin(); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto players = m_players.list(team, data::ArchivedPlayers::Include);
    if (!players) {
        return std::unexpected(players.error());
    }
    const core::Date today = m_clock.today();
    // The entries stay in this method, so their descriptions need no hiding here.
    const auto active = m_availability.listActiveOn(team, today);
    if (!active) {
        return std::unexpected(active.error());
    }
    std::vector<SquadPlayer> result;
    result.reserve(players->size());
    for (core::Player& player : *players) {
        std::vector<core::Availability> entries;
        std::ranges::copy_if(
            *active, std::back_inserter(entries),
            [&](const core::Availability& entry) { return entry.playerId == player.id; });
        const core::PlayerStatus status = core::playerStatusOn(entries, today);
        result.push_back(SquadPlayer{std::move(player), status});
    }
    return result;
}

core::Result<SquadPlayer> SquadService::player(core::PlayerId id) const {
    if (auto allowed = requireLogin(); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto player = m_players.get(id);
    if (!player) {
        return std::unexpected(player.error());
    }
    const auto entries = m_availability.listByPlayer(id);
    if (!entries) {
        return std::unexpected(entries.error());
    }
    const core::PlayerStatus status = core::playerStatusOn(*entries, m_clock.today());
    return SquadPlayer{std::move(*player), status};
}

core::Result<std::vector<core::Availability>>
SquadService::listAvailability(core::PlayerId player) const {
    if (auto allowed = requireLogin(); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto entries = m_availability.listByPlayer(player);
    if (entries && !m_session.isAllowed(core::Permission::ViewAvailabilityDetails)) {
        for (core::Availability& entry : *entries) {
            entry.description.reset();
        }
    }
    return entries;
}

core::Result<core::PlayerId> SquadService::addPlayer(core::TeamId team,
                                                     const core::PlayerDraft& draft) {
    if (auto allowed = m_session.require(core::Permission::EditSquad); !allowed) {
        return std::unexpected(allowed.error());
    }
    if (auto valid = firstError(core::validate(draft, currentYear())); !valid) {
        return std::unexpected(valid.error());
    }
    return m_players.create(team, draft);
}

core::Status SquadService::updatePlayer(core::PlayerId id, const core::PlayerDraft& draft) {
    if (auto allowed = m_session.require(core::Permission::EditSquad); !allowed) {
        return allowed;
    }
    if (auto valid = firstError(core::validate(draft, currentYear())); !valid) {
        return valid;
    }
    return m_players.update(id, draft);
}

core::Status SquadService::archivePlayer(core::PlayerId id) {
    if (auto allowed = m_session.require(core::Permission::EditSquad); !allowed) {
        return allowed;
    }
    return m_players.archive(id, m_clock.today());
}

core::Status SquadService::unarchivePlayer(core::PlayerId id) {
    if (auto allowed = m_session.require(core::Permission::EditSquad); !allowed) {
        return allowed;
    }
    return m_players.unarchive(id);
}

core::Result<core::AvailabilityId>
SquadService::addAvailability(core::PlayerId player, const core::AvailabilityDraft& draft) {
    if (auto allowed = m_session.require(core::Permission::EditAvailability); !allowed) {
        return std::unexpected(allowed.error());
    }
    if (auto described = checkDescription(draft); !described) {
        return std::unexpected(described.error());
    }
    if (auto valid = firstError(core::validate(draft)); !valid) {
        return std::unexpected(valid.error());
    }
    return m_availability.create(player, draft);
}

core::Status SquadService::updateAvailability(core::AvailabilityId id,
                                              const core::AvailabilityDraft& draft) {
    if (auto allowed = m_session.require(core::Permission::EditAvailability); !allowed) {
        return allowed;
    }
    if (auto described = checkDescription(draft); !described) {
        return described;
    }
    if (auto valid = firstError(core::validate(draft)); !valid) {
        return valid;
    }
    return m_availability.update(id, draft);
}

core::Status SquadService::markReturned(core::PlayerId player, core::AvailabilityId id,
                                        core::Date actualTo) {
    if (auto allowed = m_session.require(core::Permission::EditAvailability); !allowed) {
        return allowed;
    }
    // The repository has no get(), and the CHECK of the schema would not say which field is wrong.
    const auto entries = m_availability.listByPlayer(player);
    if (!entries) {
        return std::unexpected(entries.error());
    }
    const auto entry = std::ranges::find(*entries, id, &core::Availability::id);
    if (entry == entries->end()) {
        return std::unexpected(core::Error{
            core::ErrorCode::NotFound, "mark returned: the player has no such entry", {}});
    }
    core::AvailabilityDraft closed = *entry;
    closed.actualTo = actualTo;
    if (auto valid = firstError(core::validate(closed)); !valid) {
        return valid;
    }
    return m_availability.close(id, actualTo);
}

core::Status SquadService::requireLogin() const {
    if (m_session.user()) {
        return {};
    }
    return std::unexpected(
        core::Error{core::ErrorCode::PermissionDenied, "nobody is logged in", {}});
}

core::Status SquadService::checkDescription(const core::AvailabilityDraft& draft) const {
    if (!draft.description || m_session.isAllowed(core::Permission::ViewAvailabilityDetails)) {
        return {};
    }
    return std::unexpected(core::Error{core::ErrorCode::PermissionDenied,
                                       "a description needs ViewAvailabilityDetails",
                                       "description"});
}

int SquadService::currentYear() const { return static_cast<int>(m_clock.today().year()); }

} // namespace fdss::services
