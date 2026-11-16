#include "services/CalendarService.hpp"

#include "core/domain/Permissions.hpp"
#include "core/domain/Validation.hpp"
#include "data/db/Transaction.hpp"
#include "data/repositories/LineupRepository.hpp"
#include "data/repositories/MatchRepository.hpp"
#include "data/repositories/OpponentRepository.hpp"
#include "data/repositories/TrainingRepository.hpp"
#include "services/ServiceSupport.hpp"
#include "services/Session.hpp"

#include <algorithm>
#include <map>
#include <string_view>
#include <utility>

namespace fdss::services {

namespace {

constexpr std::string_view kOpponentName = "opponent_name";
constexpr std::string_view kMatchNotPlanned = "match_not_planned";
constexpr std::string_view kMatchInUse = "match_in_use";
constexpr std::string_view kSessionInUse = "session_in_use";

// validate() and the repository report "name" for every entity; its message speaks of 50
// characters, and an opponent may have 100.
template <class T> core::Result<T> withOpponentField(core::Result<T> result) {
    if (!result && result.error().field == "name") {
        result.error().field = kOpponentName;
    }
    return result;
}

// A repository reports every conflict of the statement alike; the caller knows which one it is.
template <class T>
core::Result<T> withConflictField(core::Result<T> result, std::string_view field) {
    if (!result && result.error().code == core::ErrorCode::Conflict) {
        result.error().field = field;
    }
    return result;
}

core::Status checkOpponentChosen(const core::MatchDraft& draft) {
    if (draft.opponentId != core::OpponentId{}) {
        return {};
    }
    return std::unexpected(
        core::Error{core::ErrorCode::InvalidInput, "no opponent chosen", "opponent_id"});
}

template <class Draft, class Row> std::vector<Draft> asDrafts(const std::vector<Row>& rows) {
    std::vector<Draft> drafts;
    drafts.reserve(rows.size());
    for (const Draft& row : rows) {
        drafts.push_back(row);
    }
    return drafts;
}

} // namespace

CalendarService::CalendarService(data::DatabaseManager& db, data::OpponentRepository& opponents,
                                 data::MatchRepository& matches, data::TrainingRepository& training,
                                 data::LineupRepository& lineups, Session& session)
    : m_db(db), m_opponents(opponents), m_matches(matches), m_training(training),
      m_lineups(lineups), m_session(session) {}

core::Result<CalendarItems> CalendarService::listItems(core::TeamId team) const {
    if (auto allowed = requireLogin(); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto matches = m_matches.list(team, {});
    if (!matches) {
        return std::unexpected(matches.error());
    }
    auto sessions = m_training.list(team, {});
    if (!sessions) {
        return std::unexpected(sessions.error());
    }
    const auto opponents = m_opponents.list();
    if (!opponents) {
        return std::unexpected(opponents.error());
    }
    std::map<core::OpponentId, std::string> names;
    for (const core::Opponent& opponent : *opponents) {
        names.emplace(opponent.id, opponent.name);
    }
    CalendarItems items;
    items.matches.reserve(matches->size());
    for (core::Match& match : *matches) {
        std::string name = names[match.opponentId];
        items.matches.push_back(CalendarMatch{std::move(match), std::move(name)});
    }
    items.sessions = std::move(*sessions);
    return items;
}

core::Result<core::Match> CalendarService::match(core::MatchId id) const {
    if (auto allowed = requireLogin(); !allowed) {
        return std::unexpected(allowed.error());
    }
    return m_matches.get(id);
}

core::Result<core::TrainingSession> CalendarService::session(core::TrainingSessionId id) const {
    if (auto allowed = requireLogin(); !allowed) {
        return std::unexpected(allowed.error());
    }
    return m_training.get(id);
}

core::Result<std::vector<core::Opponent>> CalendarService::listOpponents() const {
    if (auto allowed = requireLogin(); !allowed) {
        return std::unexpected(allowed.error());
    }
    return m_opponents.list();
}

core::Result<std::vector<std::string>> CalendarService::listCompetitions(core::TeamId team) const {
    if (auto allowed = requireLogin(); !allowed) {
        return std::unexpected(allowed.error());
    }
    return m_matches.listCompetitions(team);
}

core::Result<int> CalendarService::attendanceCount(core::TrainingSessionId id) const {
    if (auto allowed = requireLogin(); !allowed) {
        return std::unexpected(allowed.error());
    }
    const auto attendance = m_training.listAttendance(id);
    if (!attendance) {
        return std::unexpected(attendance.error());
    }
    return static_cast<int>(attendance->size());
}

core::Result<core::OpponentId> CalendarService::addOpponent(const core::OpponentDraft& draft) {
    if (auto allowed = m_session.require(core::Permission::EditCalendar); !allowed) {
        return std::unexpected(allowed.error());
    }
    if (auto valid = withOpponentField(firstError(core::validate(draft))); !valid) {
        return std::unexpected(valid.error());
    }
    return withOpponentField(m_opponents.create(draft));
}

core::Result<core::MatchId> CalendarService::addMatch(core::TeamId team,
                                                      const core::MatchDraft& draft) {
    if (auto allowed = m_session.require(core::Permission::EditCalendar); !allowed) {
        return std::unexpected(allowed.error());
    }
    if (auto chosen = checkOpponentChosen(draft); !chosen) {
        return std::unexpected(chosen.error());
    }
    if (auto valid = firstError(core::validate(draft)); !valid) {
        return std::unexpected(valid.error());
    }
    return m_matches.create(team, draft);
}

core::Status CalendarService::updateMatch(core::MatchId id, const core::MatchDraft& draft) {
    if (auto allowed = m_session.require(core::Permission::EditCalendar); !allowed) {
        return allowed;
    }
    if (auto chosen = checkOpponentChosen(draft); !chosen) {
        return chosen;
    }
    if (auto valid = firstError(core::validate(draft)); !valid) {
        return valid;
    }
    return withConflictField(m_matches.updatePlanned(id, draft), kMatchNotPlanned);
}

core::Result<DeletedMatch> CalendarService::deleteMatch(core::MatchId id) {
    if (auto allowed = m_session.require(core::Permission::EditCalendar); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    auto match = m_matches.get(id);
    if (!match) {
        return std::unexpected(match.error());
    }
    if (match->status != core::MatchStatus::Planned) {
        return std::unexpected(core::Error{core::ErrorCode::Conflict,
                                           "delete match: the match is not planned",
                                           std::string(kMatchNotPlanned)});
    }
    const auto appearances = m_matches.listAppearances(id);
    if (!appearances) {
        return std::unexpected(appearances.error());
    }
    const auto events = m_matches.listEvents(id);
    if (!events) {
        return std::unexpected(events.error());
    }
    auto lineup = m_lineups.findForMatch(id);
    if (!lineup) {
        return std::unexpected(lineup.error());
    }
    if (auto removed = m_lineups.removeForMatch(id); !removed) {
        return std::unexpected(removed.error());
    }
    if (auto removed = withConflictField(m_matches.removePlanned(id), kMatchInUse); !removed) {
        return std::unexpected(removed.error());
    }
    if (auto committed = transaction->commit(); !committed) {
        return std::unexpected(committed.error());
    }
    return DeletedMatch{std::move(*match), asDrafts<core::AppearanceDraft>(*appearances),
                        asDrafts<core::MatchEventDraft>(*events), std::move(*lineup)};
}

core::Result<core::MatchId> CalendarService::restoreMatch(const DeletedMatch& deleted) {
    if (auto allowed = m_session.require(core::Permission::EditCalendar); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto id = m_matches.create(deleted.match.teamId, deleted.match);
    if (!id) {
        return std::unexpected(id.error());
    }
    if (auto saved = m_matches.replaceAppearances(*id, deleted.appearances); !saved) {
        return std::unexpected(saved.error());
    }
    for (const core::MatchEventDraft& event : deleted.events) {
        if (const auto added = m_matches.appendEvent(*id, event); !added) {
            return std::unexpected(added.error());
        }
    }
    // Also sets match.formation again.
    if (deleted.lineup) {
        if (auto saved = m_lineups.saveForMatch(*id, *deleted.lineup); !saved) {
            return std::unexpected(saved.error());
        }
    }
    if (auto committed = transaction->commit(); !committed) {
        return std::unexpected(committed.error());
    }
    return *id;
}

core::Result<core::TrainingSessionId>
CalendarService::addSession(core::TeamId team, const core::TrainingSessionDraft& draft) {
    if (auto allowed = m_session.require(core::Permission::EditCalendar); !allowed) {
        return std::unexpected(allowed.error());
    }
    if (auto valid = firstError(core::validate(draft)); !valid) {
        return std::unexpected(valid.error());
    }
    return m_training.create(team, draft);
}

core::Status CalendarService::updateSession(core::TrainingSessionId id,
                                            const core::TrainingSessionDraft& draft) {
    if (auto allowed = m_session.require(core::Permission::EditCalendar); !allowed) {
        return allowed;
    }
    if (auto valid = firstError(core::validate(draft)); !valid) {
        return valid;
    }
    return m_training.update(id, draft);
}

core::Result<DeletedSession> CalendarService::deleteSession(core::TrainingSessionId id) {
    if (auto allowed = m_session.require(core::Permission::EditCalendar); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    auto session = m_training.get(id);
    if (!session) {
        return std::unexpected(session.error());
    }
    const auto attendance = m_training.listAttendance(id);
    if (!attendance) {
        return std::unexpected(attendance.error());
    }
    if (auto removed = withConflictField(m_training.remove(id), kSessionInUse); !removed) {
        return std::unexpected(removed.error());
    }
    if (auto committed = transaction->commit(); !committed) {
        return std::unexpected(committed.error());
    }
    return DeletedSession{std::move(*session), asDrafts<core::AttendanceDraft>(*attendance)};
}

core::Result<core::TrainingSessionId>
CalendarService::restoreSession(const DeletedSession& deleted) {
    if (auto allowed = m_session.require(core::Permission::EditCalendar); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto id = m_training.create(deleted.session.teamId, deleted.session);
    if (!id) {
        return std::unexpected(id.error());
    }
    if (auto saved = m_training.saveAttendance(*id, deleted.attendance); !saved) {
        return std::unexpected(saved.error());
    }
    if (auto committed = transaction->commit(); !committed) {
        return std::unexpected(committed.error());
    }
    return *id;
}

core::Status CalendarService::requireLogin() const {
    if (m_session.user()) {
        return {};
    }
    return std::unexpected(
        core::Error{core::ErrorCode::PermissionDenied, "nobody is logged in", {}});
}

} // namespace fdss::services
