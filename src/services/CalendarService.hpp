#pragma once

#include "core/domain/Appearance.hpp"
#include "core/domain/Attendance.hpp"
#include "core/domain/Match.hpp"
#include "core/domain/MatchEvent.hpp"
#include "core/domain/Opponent.hpp"
#include "core/domain/TrainingSession.hpp"
#include "core/util/Error.hpp"

#include <string>
#include <vector>

namespace fdss::data {
class DatabaseManager;
class MatchRepository;
class OpponentRepository;
class TrainingRepository;
} // namespace fdss::data

namespace fdss::services {

class Session;

struct CalendarMatch {
    core::Match match;
    std::string opponentName;
};

struct CalendarItems {
    std::vector<CalendarMatch> matches;
    std::vector<core::TrainingSession> sessions;
};

// What undo needs, because a delete takes the child rows with it.
struct DeletedMatch {
    core::Match match;
    std::vector<core::AppearanceDraft> appearances;
    std::vector<core::MatchEventDraft> events;
};

struct DeletedSession {
    core::TrainingSession session;
    std::vector<core::AttendanceDraft> attendance;
};

// Matches, training sessions and opponents, with the role checks of SPEC section 5. Reading needs a
// logged-in user of any role, otherwise PermissionDenied.
class CalendarService {
public:
    CalendarService(data::DatabaseManager& db, data::OpponentRepository& opponents,
                    data::MatchRepository& matches, data::TrainingRepository& training,
                    Session& session);

    [[nodiscard]] core::Result<CalendarItems> listItems(core::TeamId team) const;
    [[nodiscard]] core::Result<core::Match> match(core::MatchId id) const;
    [[nodiscard]] core::Result<core::TrainingSession> session(core::TrainingSessionId id) const;
    [[nodiscard]] core::Result<std::vector<core::Opponent>> listOpponents() const;
    [[nodiscard]] core::Result<std::vector<std::string>> listCompetitions(core::TeamId team) const;
    // How many players have attendance saved, so the view can ask before a delete.
    [[nodiscard]] core::Result<int> attendanceCount(core::TrainingSessionId id) const;

    // The methods below need EditCalendar.
    // A bad or duplicate name: InvalidInput or Conflict with field "opponent_name".
    [[nodiscard]] core::Result<core::OpponentId> addOpponent(const core::OpponentDraft& draft);
    // No opponent chosen: InvalidInput with field "opponent_id".
    [[nodiscard]] core::Result<core::MatchId> addMatch(core::TeamId team,
                                                       const core::MatchDraft& draft);
    // Not planned: Conflict with field "match_not_planned".
    [[nodiscard]] core::Status updateMatch(core::MatchId id, const core::MatchDraft& draft);
    // Not planned: Conflict with field "match_not_planned". Other rows refer to it: Conflict with
    // field "match_in_use".
    [[nodiscard]] core::Result<DeletedMatch> deleteMatch(core::MatchId id);
    // A new row with a new id. Its import_id and formation stay empty: the repository cannot set
    // them.
    [[nodiscard]] core::Result<core::MatchId> restoreMatch(const DeletedMatch& deleted);
    [[nodiscard]] core::Result<core::TrainingSessionId>
    addSession(core::TeamId team, const core::TrainingSessionDraft& draft);
    [[nodiscard]] core::Status updateSession(core::TrainingSessionId id,
                                             const core::TrainingSessionDraft& draft);
    // Metric values refer to it: Conflict with field "session_in_use".
    [[nodiscard]] core::Result<DeletedSession> deleteSession(core::TrainingSessionId id);
    // A new row with a new id. Its import_id stays empty: the repository cannot set it.
    [[nodiscard]] core::Result<core::TrainingSessionId>
    restoreSession(const DeletedSession& deleted);

private:
    [[nodiscard]] core::Status requireLogin() const;

    data::DatabaseManager& m_db;
    data::OpponentRepository& m_opponents;
    data::MatchRepository& m_matches;
    data::TrainingRepository& m_training;
    Session& m_session;
};

} // namespace fdss::services
