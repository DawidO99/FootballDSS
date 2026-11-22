#pragma once

#include "core/domain/Metric.hpp"
#include "core/domain/Player.hpp"
#include "core/util/Error.hpp"

#include <optional>
#include <span>
#include <vector>

namespace fdss::data {
class DatabaseManager;
class MatchRepository;
class MetricRepository;
class PlayerRepository;
class TrainingRepository;
} // namespace fdss::data

namespace fdss::services {

class Clock;
class Session;

struct MetricDefinitionInfo {
    core::MetricDefinition definition;
    // Values block the delete and the change of type and scope.
    int valueCount = 0;
};

struct MetricSheetRow {
    core::Player player;
    std::optional<double> value;
};

// The entry grid of one metric for one match, session or day.
struct MetricSheet {
    core::MetricDefinition metric;
    core::MetricTarget target;
    // Of the match or the session; the day itself for scope player.
    core::Date date{};
    // Players of the team who are not archived.
    std::vector<MetricSheetRow> rows;
};

struct MetricRowError {
    core::PlayerId player;
    core::Error error;
};

// What the player card shows of one metric.
struct PlayerMetricSummary {
    core::MetricDefinition metric;
    double latest = 0.0;
    core::Date latestDate{};
    // The value before the latest one, also from before the window.
    std::optional<double> previous;
    // The aggregation of the metric over the last kMetricWindowDays days; nullopt without a value
    // in that window.
    std::optional<double> window;
    int windowCount = 0;
    core::MetricTrend trend = core::MetricTrend::None;
};

// Custom metrics with the role checks of SPEC section 5: reading needs a logged-in user of any
// role, changes need ManageMetrics; otherwise PermissionDenied.
class MetricService {
public:
    MetricService(data::DatabaseManager& db, data::MetricRepository& metrics,
                  data::PlayerRepository& players, data::TrainingRepository& training,
                  data::MatchRepository& matches, Session& session, const Clock& clock);

    // In the order they were created.
    [[nodiscard]] core::Result<std::vector<MetricDefinitionInfo>> listDefinitions() const;
    [[nodiscard]] core::Result<core::MetricDefinition>
    definition(core::MetricDefinitionId id) const;

    // core::validate() errors as InvalidInput with their field. A Polish or English name that is
    // the same as a name of another metric in either language (core::metricNameKey()): Conflict
    // with field "name_pl" or "name_en". The key comes from the name (core::metricKey()); a key
    // that is taken: Conflict with field "key". The same name check holds for the update (the
    // edited metric aside) and the restore.
    [[nodiscard]] core::Result<core::MetricDefinitionId>
    addDefinition(const core::MetricDefinitionDraft& draft);
    // With values, the value type and the scope stay: Conflict with field "metric_locked".
    [[nodiscard]] core::Status updateDefinition(core::MetricDefinitionId id,
                                                const core::MetricDefinitionDraft& draft);
    // With values: Conflict with field "metric_in_use". Returns what restoreDefinition() needs.
    [[nodiscard]] core::Result<core::MetricDefinition>
    deleteDefinition(core::MetricDefinitionId id);
    // The undo of deleteDefinition(): the same key, a new id.
    [[nodiscard]] core::Result<core::MetricDefinitionId>
    restoreDefinition(const core::MetricDefinition& deleted);

    // The target must fit the scope of the metric (InvalidInput, field "scope") and a match or a
    // session must be of `team` (InvalidInput, field "team"); a missing one: NotFound.
    [[nodiscard]] core::Result<MetricSheet> sheet(core::MetricDefinitionId id, core::TeamId team,
                                                  const core::MetricTarget& target) const;
    // Needs ManageMetrics. An empty list: saved. Otherwise nothing was written, and there is one
    // entry per wrong row: field "player_id" for a player given twice or from another team, the
    // fields of core::validateValue(). A failure of another kind is the unexpected value.
    [[nodiscard]] core::Result<std::vector<MetricRowError>>
    saveValues(core::MetricDefinitionId id, core::TeamId team, const core::MetricTarget& target,
               std::span<const core::MetricValueDraft> values);

    // Every metric with a value of the player on or before today, in the order of the definitions.
    [[nodiscard]] core::Result<std::vector<PlayerMetricSummary>>
    playerSummary(core::PlayerId player) const;

private:
    [[nodiscard]] core::Status requireLogin() const;
    [[nodiscard]] core::Status checkNamesFree(const core::MetricDefinitionDraft& draft,
                                              std::optional<core::MetricDefinitionId> except) const;
    // The date of the entry, after the checks of sheet().
    [[nodiscard]] core::Result<core::Date> targetDate(const core::MetricDefinition& metric,
                                                      core::TeamId team,
                                                      const core::MetricTarget& target) const;

    data::DatabaseManager& m_db;
    data::MetricRepository& m_metrics;
    data::PlayerRepository& m_players;
    data::TrainingRepository& m_training;
    data::MatchRepository& m_matches;
    Session& m_session;
    const Clock& m_clock;
};

} // namespace fdss::services
