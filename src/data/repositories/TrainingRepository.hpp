#pragma once

#include "core/domain/Attendance.hpp"
#include "core/domain/DateTypes.hpp"
#include "core/domain/TrainingSession.hpp"
#include "core/util/Error.hpp"

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
    [[nodiscard]] core::Result<core::TrainingSessionId>
    create(core::TeamId team, const core::TrainingSessionDraft& draft);
    [[nodiscard]] core::Status update(core::TrainingSessionId id,
                                      const core::TrainingSessionDraft& draft);
    // Attendance goes with the session. A session with metric values: Conflict.
    [[nodiscard]] core::Status remove(core::TrainingSessionId id);

    // Replaces all attendance of the session in one transaction: on any error the old rows stay.
    [[nodiscard]] core::Status saveAttendance(core::TrainingSessionId session,
                                              std::span<const core::AttendanceDraft> attendance);
    [[nodiscard]] core::Result<std::vector<core::Attendance>>
    listAttendance(core::TrainingSessionId session) const;
    // Sorted by the date of the session.
    [[nodiscard]] core::Result<std::vector<core::PlayerAttendance>>
    listAttendanceByPlayer(core::PlayerId player, core::DateRange range) const;

private:
    DatabaseManager& m_db;
};

} // namespace fdss::data
