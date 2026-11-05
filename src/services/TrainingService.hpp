#pragma once

#include "core/domain/Attendance.hpp"
#include "core/domain/Player.hpp"
#include "core/domain/TrainingSession.hpp"
#include "core/util/Error.hpp"

#include <span>
#include <vector>

namespace fdss::data {
class AvailabilityRepository;
class DatabaseManager;
class PlayerRepository;
class TrainingRepository;
} // namespace fdss::data

namespace fdss::services {

class Session;

struct AttendanceSheetRow {
    core::Player player;
    // The saved row, or defaultStatus with nothing else filled in.
    core::AttendanceDraft draft;
    // A row for this player exists in the database.
    bool saved = false;
    // What the availability on the date of the session says: injury or illness, then injured;
    // absence, then excused; otherwise present. A suspended player still trains.
    core::AttendanceStatus defaultStatus = core::AttendanceStatus::Present;
};

struct AttendanceSheet {
    core::TrainingSession session;
    // Players who are not archived.
    std::vector<AttendanceSheetRow> rows;
};

struct AttendanceRowError {
    core::PlayerId player;
    core::Error error;
};

// Attendance and RPE of a training session, with the role checks of SPEC section 5. Reading needs a
// logged-in user of any role, otherwise PermissionDenied.
class TrainingService {
public:
    TrainingService(data::DatabaseManager& db, data::TrainingRepository& training,
                    data::PlayerRepository& players, data::AvailabilityRepository& availability,
                    Session& session);

    // Newest first.
    [[nodiscard]] core::Result<std::vector<core::TrainingSession>>
    listSessions(core::TeamId team) const;
    [[nodiscard]] core::Result<AttendanceSheet> sheet(core::TrainingSessionId id) const;

    // Needs EditTraining. An empty list: saved. Otherwise nothing was written, and there is one
    // entry per wrong row: core::validate(), and field "player_id" for a player given twice or from
    // another team. A failure of another kind (permission, database) is the unexpected value. The
    // saved rows of players who are not in `attendance` stay: the sheet does not show archived
    // players, and their history must survive another save of the session.
    [[nodiscard]] core::Result<std::vector<AttendanceRowError>>
    saveAttendance(core::TrainingSessionId id, std::span<const core::AttendanceDraft> attendance);

private:
    [[nodiscard]] core::Status requireLogin() const;

    data::DatabaseManager& m_db;
    data::TrainingRepository& m_training;
    data::PlayerRepository& m_players;
    data::AvailabilityRepository& m_availability;
    Session& m_session;
};

} // namespace fdss::services
