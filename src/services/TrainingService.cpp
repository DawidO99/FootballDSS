#include "services/TrainingService.hpp"

#include "core/domain/Availability.hpp"
#include "core/domain/Permissions.hpp"
#include "core/domain/Validation.hpp"
#include "data/db/Transaction.hpp"
#include "data/repositories/AvailabilityRepository.hpp"
#include "data/repositories/PlayerRepository.hpp"
#include "data/repositories/TrainingRepository.hpp"
#include "services/ServiceSupport.hpp"
#include "services/Session.hpp"

#include <algorithm>
#include <map>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace fdss::services {

namespace {

constexpr std::string_view kPlayerId = "player_id";

// Not core::playerStatusOn(): its order puts a suspension above an absence, and here an absence
// decides whenever it is there, because a suspension says nothing about training.
core::AttendanceStatus defaultStatus(std::span<const core::Availability> entries,
                                     core::PlayerId player) {
    const auto hasType = [&](core::AvailabilityType type) {
        return std::ranges::any_of(entries, [&](const core::Availability& entry) {
            return entry.playerId == player && entry.type == type;
        });
    };
    if (hasType(core::AvailabilityType::Injury) || hasType(core::AvailabilityType::Illness)) {
        return core::AttendanceStatus::Injured;
    }
    if (hasType(core::AvailabilityType::Absence)) {
        return core::AttendanceStatus::Excused;
    }
    return core::AttendanceStatus::Present;
}

core::Error playerError(std::string_view detail) {
    return core::Error{core::ErrorCode::InvalidInput, std::string(detail), std::string(kPlayerId)};
}

} // namespace

TrainingService::TrainingService(data::DatabaseManager& db, data::TrainingRepository& training,
                                 data::PlayerRepository& players,
                                 data::AvailabilityRepository& availability, Session& session)
    : m_db(db), m_training(training), m_players(players), m_availability(availability),
      m_session(session) {}

core::Result<std::vector<core::TrainingSession>>
TrainingService::listSessions(core::TeamId team) const {
    if (auto allowed = requireLogin(); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto sessions = m_training.list(team, {});
    if (sessions) {
        std::ranges::reverse(*sessions);
    }
    return sessions;
}

core::Result<AttendanceSheet> TrainingService::sheet(core::TrainingSessionId id) const {
    if (auto allowed = requireLogin(); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto session = m_training.get(id);
    if (!session) {
        return std::unexpected(session.error());
    }
    auto players = m_players.list(session->teamId, data::ArchivedPlayers::Exclude);
    if (!players) {
        return std::unexpected(players.error());
    }
    // The entries stay in this method, so their descriptions need no hiding here.
    const auto active = m_availability.listActiveOn(session->teamId, session->date);
    if (!active) {
        return std::unexpected(active.error());
    }
    const auto saved = m_training.listAttendance(id);
    if (!saved) {
        return std::unexpected(saved.error());
    }
    std::map<core::PlayerId, core::AttendanceDraft> savedByPlayer;
    for (const core::Attendance& row : *saved) {
        savedByPlayer.emplace(row.playerId, row);
    }
    AttendanceSheet sheet;
    sheet.session = std::move(*session);
    sheet.rows.reserve(players->size());
    for (core::Player& player : *players) {
        AttendanceSheetRow row;
        row.defaultStatus = defaultStatus(*active, player.id);
        if (const auto found = savedByPlayer.find(player.id); found != savedByPlayer.end()) {
            row.draft = found->second;
            row.saved = true;
        } else {
            row.draft.playerId = player.id;
            row.draft.status = row.defaultStatus;
        }
        row.player = std::move(player);
        sheet.rows.push_back(std::move(row));
    }
    return sheet;
}

core::Result<std::vector<AttendanceRowError>>
TrainingService::saveAttendance(core::TrainingSessionId id,
                                std::span<const core::AttendanceDraft> attendance) {
    if (auto allowed = m_session.require(core::Permission::EditTraining); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto session = m_training.get(id);
    if (!session) {
        return std::unexpected(session.error());
    }
    const auto players = m_players.list(session->teamId, data::ArchivedPlayers::Include);
    if (!players) {
        return std::unexpected(players.error());
    }
    std::set<core::PlayerId> teamPlayers;
    for (const core::Player& player : *players) {
        teamPlayers.insert(player.id);
    }

    std::vector<AttendanceRowError> errors;
    std::set<core::PlayerId> given;
    for (const core::AttendanceDraft& row : attendance) {
        if (!teamPlayers.contains(row.playerId)) {
            errors.push_back(
                {row.playerId, playerError("the player is not of the session's team")});
        } else if (!given.insert(row.playerId).second) {
            errors.push_back({row.playerId, playerError("the player is given twice")});
        } else if (auto valid = firstError(core::validate(row)); !valid) {
            errors.push_back({row.playerId, valid.error()});
        }
    }
    if (!errors.empty()) {
        return errors;
    }

    const auto stored = m_training.listAttendance(id);
    if (!stored) {
        return std::unexpected(stored.error());
    }
    std::vector<core::AttendanceDraft> rows(attendance.begin(), attendance.end());
    for (const core::Attendance& row : *stored) {
        if (!given.contains(row.playerId)) {
            rows.push_back(row);
        }
    }
    if (auto saved = m_training.saveAttendance(id, rows); !saved) {
        return std::unexpected(saved.error());
    }
    if (auto committed = transaction->commit(); !committed) {
        return std::unexpected(committed.error());
    }
    return errors;
}

core::Status TrainingService::requireLogin() const {
    if (m_session.user()) {
        return {};
    }
    return std::unexpected(
        core::Error{core::ErrorCode::PermissionDenied, "nobody is logged in", {}});
}

} // namespace fdss::services
