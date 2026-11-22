#pragma once

#include "core/domain/Attendance.hpp"
#include "core/domain/DateTypes.hpp"
#include "core/domain/TrainingSession.hpp"
#include "core/util/Error.hpp"

#include <optional>
#include <span>
#include <vector>

namespace fdss::data {

class DatabaseManager;

class TrainingRepository {
public:
    explicit TrainingRepository(DatabaseManager& db) : m_db(db) {}

    [[nodiscard]] core::Result<std::vector<core::TrainingSession>>
    list(core::TeamId team, core::DateRange range) const;
    [[nodiscard]] core::Result<core::TrainingSession> get(core::TrainingSessionId id) const;
    // `importId`: the import that creates the session (packet P18).
    [[nodiscard]] core::Result<core::TrainingSessionId>
    create(core::TeamId team, const core::TrainingSessionDraft& draft,
           std::optional<core::ImportId> importId = std::nullopt);
    [[nodiscard]] core::Status update(core::TrainingSessionId id,
                                      const core::TrainingSessionDraft& draft);
    // Attendance goes with the session. A session with metric values: Conflict.
    [[nodiscard]] core::Status remove(core::TrainingSessionId id);

    // Replaces all attendance of the session in one transaction: on any error the old rows stay.
    // A player whose row an import created keeps its import_id, so the undo of that import still
    // removes the corrected row (packet P18 decision 11).
    [[nodiscard]] core::Status saveAttendance(core::TrainingSessionId session,
                                              std::span<const core::AttendanceDraft> attendance);
    [[nodiscard]] core::Result<std::vector<core::Attendance>>
    listAttendance(core::TrainingSessionId session) const;
    // Sorted by the date of the session.
    [[nodiscard]] core::Result<std::vector<core::PlayerAttendance>>
    listAttendanceByPlayer(core::PlayerId player, core::DateRange range) const;

    // --- Import (packet P18). Each joins the transaction of the caller. ---
    // The row of one player, or nullopt.
    [[nodiscard]] core::Result<std::optional<core::Attendance>>
    findAttendance(core::TrainingSessionId session, core::PlayerId player) const;
    [[nodiscard]] core::Status insertAttendance(core::TrainingSessionId session,
                                                const core::AttendanceDraft& draft,
                                                std::optional<core::ImportId> importId);
    // The row of draft.playerId; its id and import_id stay. None: NotFound.
    [[nodiscard]] core::Status updateAttendance(core::TrainingSessionId session,
                                                const core::AttendanceDraft& draft);
    // The sessions the import created.
    [[nodiscard]] core::Result<std::vector<core::TrainingSession>>
    listByImport(core::ImportId importId) const;
    // Attendance rows in the sessions of the import that the import did not write (added later).
    [[nodiscard]] core::Result<int> countAttendanceAddedLater(core::ImportId importId) const;
    // Deletes the attendance rows the import created, in any session; returns how many.
    [[nodiscard]] core::Result<int> removeAttendanceByImport(core::ImportId importId);
    // Deletes the sessions the import created with their attendance (metric values must be gone).
    [[nodiscard]] core::Result<int> removeByImport(core::ImportId importId);

private:
    DatabaseManager& m_db;
};

} // namespace fdss::data
