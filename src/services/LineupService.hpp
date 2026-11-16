#pragma once

#include "core/algorithms/LineupValidator.hpp"
#include "core/domain/DateTypes.hpp"
#include "core/domain/Ids.hpp"
#include "core/domain/Lineup.hpp"
#include "core/domain/Match.hpp"
#include "core/domain/Player.hpp"
#include "core/util/Error.hpp"
#include "services/CalendarService.hpp"

#include <QString>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fdss::data {
class AvailabilityRepository;
class DatabaseManager;
class LineupRepository;
class MatchRepository;
class OpponentRepository;
class PlayerRepository;
class TrainingRepository;
} // namespace fdss::data

namespace fdss::services {

class Clock;
class Session;

struct LineupCandidate {
    core::Player player;
    // Without the load status: that comes from LoadService.
    core::LineupPlayerFacts facts;
};

// What Pre-match reads for one match.
struct MatchLineup {
    CalendarMatch match;
    // The last finished match of the team against this opponent before this one.
    std::optional<core::Match> lastMeeting;
    // Players who are not archived, in the order of the repository.
    std::vector<LineupCandidate> candidates;
    // The facts of the candidates and the threshold, without load statuses.
    core::LineupContext context;
    // The saved lineup, or an empty one in match.formation or else 4-4-2.
    core::Lineup lineup;
    bool saved = false;
    // Places of the saved lineup whose player is no longer in the squad; they were left out.
    int droppedPlayers = 0;
};

struct LoadedLineup {
    core::Lineup lineup;
    // Places whose player is not in the squad of the team; they were left out.
    int droppedPlayers = 0;
};

struct LineupTemplateInfo {
    core::LineupId id;
    std::string name;
    std::string formation;

    friend bool operator==(const LineupTemplateInfo&, const LineupTemplateInfo&) = default;
};

// What undo needs.
struct DeletedTemplate {
    std::string name;
    core::Lineup lineup;
};

// Pre-match (SPEC section 7.2 and section 4, packet P12): the facts the lineup validator needs, the
// lineup of a match, templates and `.tactic.json`. Reading needs a logged-in user of any role,
// otherwise PermissionDenied; saving, templates and importing need EditLineup (P12 decision 14).
// Reads on the calling thread: a few small queries (P12 decision 4).
class LineupService {
public:
    static constexpr int kMaxTemplateNameLength = 50;

    LineupService(data::DatabaseManager& db, data::LineupRepository& lineups,
                  data::MatchRepository& matches, data::OpponentRepository& opponents,
                  data::PlayerRepository& players, data::AvailabilityRepository& availability,
                  data::TrainingRepository& training, Session& session, const Clock& clock);

    // Every planned match of the team, by date.
    [[nodiscard]] core::Result<std::vector<CalendarMatch>> plannedMatches(core::TeamId team) const;
    // The first planned match on or after today, otherwise the last planned one; nullopt: none.
    [[nodiscard]] core::Result<std::optional<core::MatchId>>
    nextPlannedMatch(core::TeamId team) const;
    // Any match of any status: a match that is not planned is shown read-only.
    [[nodiscard]] core::Result<MatchLineup> matchLineup(core::MatchId match,
                                                        int yellowCardThreshold) const;
    // The rules of SPEC section 7.2. Pure; the load statuses come in the context.
    [[nodiscard]] std::vector<core::LineupIssue> check(const core::Lineup& lineup,
                                                       const core::LineupContext& context) const;
    // Sorted by the name as stored; the view sorts for the language.
    [[nodiscard]] core::Result<std::vector<LineupTemplateInfo>> templates() const;
    // Every role: exporting reads only. Cannot be written: Io with field "tactic_file".
    [[nodiscard]] core::Status exportTactic(const QString& path, const core::Lineup& lineup) const;

    // The methods below need EditLineup.
    //
    // Also with errors of the validator: a draft (Match Day refuses to start with errors). A match
    // that is not planned: Conflict with field "match_not_planned". A lineup that is not 11 slots
    // with at most one goalkeeper, coordinates 0–1 and at most 12 on the bench: InvalidInput with
    // field "lineup_shape". The name of the shape is counted again from the slots.
    [[nodiscard]] core::Status save(core::MatchId match, const core::Lineup& lineup);
    // Players who are not in the squad of the team are left out.
    [[nodiscard]] core::Result<LoadedLineup> loadTemplate(core::LineupId id,
                                                          core::TeamId team) const;
    // The name is trimmed: 1–50 characters, otherwise InvalidInput with field "template_name". A
    // name already taken, regardless of case: Conflict with field "template_name", unless
    // `replace`, which replaces that template and returns its id.
    [[nodiscard]] core::Result<core::LineupId>
    saveTemplate(std::string_view name, const core::Lineup& lineup, bool replace);
    [[nodiscard]] core::Result<DeletedTemplate> deleteTemplate(core::LineupId id);
    // A new row with a new id. The name taken meanwhile: Conflict with field "template_name".
    [[nodiscard]] core::Result<core::LineupId> restoreTemplate(const DeletedTemplate& deleted);
    // Nothing is written; players who are not in the squad of the team are left out. The errors of
    // data::readTacticFile().
    [[nodiscard]] core::Result<LoadedLineup> importTactic(const QString& path,
                                                          core::TeamId team) const;

private:
    [[nodiscard]] core::Status requireLogin() const;
    // Ids of the players of the team who are not archived.
    [[nodiscard]] core::Result<std::set<core::PlayerId>> squadOf(core::TeamId team) const;
    [[nodiscard]] core::Result<std::optional<core::LineupId>>
    findTemplate(std::string_view name) const;
    // Players without a saved status other than present in each of the last kRecentSessions
    // sessions with saved attendance before `before` and not after today.
    [[nodiscard]] core::Result<std::set<core::PlayerId>>
    missedRecentSessions(core::TeamId team, core::Date before) const;
    // Yellow cards per player in matches of the team and competition before the match, without
    // matches in which the player got a red card (P12 decision 3).
    [[nodiscard]] core::Result<std::map<core::PlayerId, int>>
    yellowCards(const core::Match& match) const;

    data::DatabaseManager& m_db;
    data::LineupRepository& m_lineups;
    data::MatchRepository& m_matches;
    data::OpponentRepository& m_opponents;
    data::PlayerRepository& m_players;
    data::AvailabilityRepository& m_availability;
    data::TrainingRepository& m_training;
    Session& m_session;
    const Clock& m_clock;
    core::LineupValidator m_validator;
};

// Leaves out the places whose player is not in `squad`; returns how many.
[[nodiscard]] int dropOutsiders(core::Lineup& lineup, const std::set<core::PlayerId>& squad);

} // namespace fdss::services
