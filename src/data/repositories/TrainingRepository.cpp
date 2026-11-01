#include "data/repositories/TrainingRepository.hpp"

#include "data/db/Transaction.hpp"
#include "data/repositories/RepositorySupport.hpp"

#include <string_view>

namespace fdss::data {

namespace {

const auto kSelectSession = QStringLiteral(
    "SELECT id, team_id, date, start_time, duration_min, type, note FROM training_session ");

core::TrainingSession readSession(RowReader& row) {
    core::TrainingSession session;
    session.id = row.id<core::TrainingSessionId>();
    session.teamId = row.id<core::TeamId>();
    session.date = row.date();
    session.startTime = row.optionalTime();
    session.durationMin = row.integer();
    session.type = row.enumeration<core::SessionType>();
    session.note = row.optionalText();
    return session;
}

core::Attendance readAttendance(RowReader& row) {
    core::Attendance attendance;
    attendance.id = row.id<core::AttendanceId>();
    attendance.sessionId = row.id<core::TrainingSessionId>();
    attendance.playerId = row.id<core::PlayerId>();
    attendance.status = row.enumeration<core::AttendanceStatus>();
    attendance.rpe = row.optionalInteger();
    attendance.minutesOverride = row.optionalInteger();
    return attendance;
}

core::PlayerAttendance readPlayerAttendance(RowReader& row) {
    core::PlayerAttendance result;
    result.attendance = readAttendance(row);
    result.date = row.date();
    result.durationMin = row.integer();
    return result;
}

} // namespace

core::Result<std::vector<core::TrainingSession>>
TrainingRepository::list(core::TeamId team, core::DateRange range) const {
    return selectAll(m_db,
                     kSelectSession + QStringLiteral("WHERE team_id = ? "
                                                     "AND date >= COALESCE(?, '0000-01-01') "
                                                     "AND date <= COALESCE(?, '9999-12-31') "
                                                     "ORDER BY date, start_time, id"),
                     {idValue(team), optionalDateValue(range.from), optionalDateValue(range.to)},
                     "list sessions", readSession);
}

core::Result<core::TrainingSession> TrainingRepository::get(core::TrainingSessionId id) const {
    return selectOne(m_db, kSelectSession + QStringLiteral("WHERE id = ?"), {idValue(id)},
                     "get session", readSession);
}

core::Result<core::TrainingSessionId>
TrainingRepository::create(core::TeamId team, const core::TrainingSessionDraft& draft) {
    const auto id =
        insertRow(m_db,
                  QStringLiteral("INSERT INTO training_session (team_id, date, start_time, "
                                 "duration_min, type, note) VALUES (?, ?, ?, ?, ?, ?)"),
                  {idValue(team), dateValue(draft.date), optionalTimeValue(draft.startTime),
                   draft.durationMin, enumValue(draft.type), optionalTextValue(draft.note)},
                  "create session");
    if (!id) {
        return std::unexpected(id.error());
    }
    return core::TrainingSessionId{*id};
}

core::Status TrainingRepository::update(core::TrainingSessionId id,
                                        const core::TrainingSessionDraft& draft) {
    return changeRow(m_db,
                     QStringLiteral("UPDATE training_session SET date = ?, start_time = ?, "
                                    "duration_min = ?, type = ?, note = ? WHERE id = ?"),
                     {dateValue(draft.date), optionalTimeValue(draft.startTime), draft.durationMin,
                      enumValue(draft.type), optionalTextValue(draft.note), idValue(id)},
                     "update session");
}

core::Status TrainingRepository::remove(core::TrainingSessionId id) {
    return changeRow(m_db, QStringLiteral("DELETE FROM training_session WHERE id = ?"),
                     {idValue(id)}, "delete session", ForeignKeyFailure::RowInUse);
}

core::Status TrainingRepository::saveAttendance(core::TrainingSessionId session,
                                                std::span<const core::AttendanceDraft> attendance) {
    constexpr std::string_view kContext = "save attendance";
    auto transaction = Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    // Without this an empty list for a missing session would look like a success.
    if (auto exists =
            requireRow(m_db, QStringLiteral("SELECT 1 FROM training_session WHERE id = ?"),
                       {idValue(session)}, kContext);
        !exists) {
        return exists;
    }
    if (const auto deleted =
            runSql(m_db, QStringLiteral("DELETE FROM attendance WHERE session_id = ?"),
                   {idValue(session)}, kContext);
        !deleted) {
        return std::unexpected(deleted.error());
    }
    for (const core::AttendanceDraft& row : attendance) {
        const auto inserted =
            insertRow(m_db,
                      QStringLiteral("INSERT INTO attendance (session_id, player_id, status, rpe, "
                                     "minutes_override) VALUES (?, ?, ?, ?, ?)"),
                      {idValue(session), idValue(row.playerId), enumValue(row.status),
                       optionalValue(row.rpe), optionalValue(row.minutesOverride)},
                      kContext);
        if (!inserted) {
            return std::unexpected(inserted.error());
        }
    }
    return transaction->commit();
}

core::Result<std::vector<core::Attendance>>
TrainingRepository::listAttendance(core::TrainingSessionId session) const {
    return selectAll(m_db,
                     QStringLiteral("SELECT id, session_id, player_id, status, rpe, "
                                    "minutes_override FROM attendance WHERE session_id = ? "
                                    "ORDER BY id"),
                     {idValue(session)}, "list attendance of a session", readAttendance);
}

core::Result<std::vector<core::PlayerAttendance>>
TrainingRepository::listAttendanceByPlayer(core::PlayerId player, core::DateRange range) const {
    return selectAll(m_db,
                     QStringLiteral("SELECT a.id, a.session_id, a.player_id, a.status, a.rpe, "
                                    "a.minutes_override, s.date, s.duration_min "
                                    "FROM attendance AS a "
                                    "JOIN training_session AS s ON s.id = a.session_id "
                                    "WHERE a.player_id = ? "
                                    "AND s.date >= COALESCE(?, '0000-01-01') "
                                    "AND s.date <= COALESCE(?, '9999-12-31') "
                                    "ORDER BY s.date, s.start_time, a.id"),
                     {idValue(player), optionalDateValue(range.from), optionalDateValue(range.to)},
                     "list attendance of a player", readPlayerAttendance);
}

} // namespace fdss::data
