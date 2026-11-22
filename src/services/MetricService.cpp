#include "services/MetricService.hpp"

#include "core/domain/Permissions.hpp"
#include "core/domain/Validation.hpp"
#include "data/db/Transaction.hpp"
#include "data/repositories/MatchRepository.hpp"
#include "data/repositories/MetricRepository.hpp"
#include "data/repositories/PlayerRepository.hpp"
#include "data/repositories/TrainingRepository.hpp"
#include "services/Clock.hpp"
#include "services/ServiceSupport.hpp"
#include "services/Session.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace fdss::services {

namespace {

core::Error error(core::ErrorCode code, std::string_view detail, std::string_view field) {
    return core::Error{code, std::string(detail), std::string(field)};
}

} // namespace

MetricService::MetricService(data::DatabaseManager& db, data::MetricRepository& metrics,
                             data::PlayerRepository& players, data::TrainingRepository& training,
                             data::MatchRepository& matches, Session& session, const Clock& clock)
    : m_db(db), m_metrics(metrics), m_players(players), m_training(training), m_matches(matches),
      m_session(session), m_clock(clock) {}

core::Result<std::vector<MetricDefinitionInfo>> MetricService::listDefinitions() const {
    if (auto allowed = requireLogin(); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto definitions = m_metrics.listDefinitions();
    if (!definitions) {
        return std::unexpected(definitions.error());
    }
    std::vector<MetricDefinitionInfo> result;
    result.reserve(definitions->size());
    for (core::MetricDefinition& definition : *definitions) {
        const auto count = m_metrics.valueCount(definition.id);
        if (!count) {
            return std::unexpected(count.error());
        }
        result.push_back({std::move(definition), *count});
    }
    return result;
}

core::Result<core::MetricDefinition> MetricService::definition(core::MetricDefinitionId id) const {
    if (auto allowed = requireLogin(); !allowed) {
        return std::unexpected(allowed.error());
    }
    return m_metrics.getDefinition(id);
}

core::Result<core::MetricDefinitionId>
MetricService::addDefinition(const core::MetricDefinitionDraft& draft) {
    if (auto allowed = m_session.require(core::Permission::ManageMetrics); !allowed) {
        return std::unexpected(allowed.error());
    }
    if (auto valid = firstError(core::validate(draft)); !valid) {
        return std::unexpected(valid.error());
    }
    if (auto free = checkNamesFree(draft, std::nullopt); !free) {
        return std::unexpected(free.error());
    }
    const std::string key = core::metricKey(draft);
    const auto taken = m_metrics.keyExists(key);
    if (!taken) {
        return std::unexpected(taken.error());
    }
    if (*taken) {
        return std::unexpected(error(core::ErrorCode::Conflict, "the metric key is taken", "key"));
    }
    return m_metrics.createDefinition(draft, key);
}

core::Status MetricService::updateDefinition(core::MetricDefinitionId id,
                                             const core::MetricDefinitionDraft& draft) {
    if (auto allowed = m_session.require(core::Permission::ManageMetrics); !allowed) {
        return allowed;
    }
    if (auto valid = firstError(core::validate(draft)); !valid) {
        return valid;
    }
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto stored = m_metrics.getDefinition(id);
    if (!stored) {
        return std::unexpected(stored.error());
    }
    if (auto free = checkNamesFree(draft, id); !free) {
        return free;
    }
    if (stored->valueType != draft.valueType || stored->scope != draft.scope) {
        const auto count = m_metrics.valueCount(id);
        if (!count) {
            return std::unexpected(count.error());
        }
        if (*count > 0) {
            return std::unexpected(error(core::ErrorCode::Conflict,
                                         "a metric with values keeps its type and scope",
                                         "metric_locked"));
        }
    }
    if (auto updated = m_metrics.updateDefinition(id, draft); !updated) {
        return updated;
    }
    return transaction->commit();
}

core::Result<core::MetricDefinition> MetricService::deleteDefinition(core::MetricDefinitionId id) {
    if (auto allowed = m_session.require(core::Permission::ManageMetrics); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    auto stored = m_metrics.getDefinition(id);
    if (!stored) {
        return std::unexpected(stored.error());
    }
    const auto count = m_metrics.valueCount(id);
    if (!count) {
        return std::unexpected(count.error());
    }
    if (*count > 0) {
        return std::unexpected(
            error(core::ErrorCode::Conflict, "the metric has values", "metric_in_use"));
    }
    if (auto removed = m_metrics.removeDefinition(id); !removed) {
        return std::unexpected(removed.error());
    }
    if (auto committed = transaction->commit(); !committed) {
        return std::unexpected(committed.error());
    }
    return stored;
}

core::Result<core::MetricDefinitionId>
MetricService::restoreDefinition(const core::MetricDefinition& deleted) {
    if (auto allowed = m_session.require(core::Permission::ManageMetrics); !allowed) {
        return std::unexpected(allowed.error());
    }
    if (auto valid = firstError(core::validate(deleted)); !valid) {
        return std::unexpected(valid.error());
    }
    // Another metric may have taken the name since the delete.
    if (auto free = checkNamesFree(deleted, std::nullopt); !free) {
        return std::unexpected(free.error());
    }
    return m_metrics.createDefinition(deleted, deleted.key);
}

core::Result<MetricSheet> MetricService::sheet(core::MetricDefinitionId id, core::TeamId team,
                                               const core::MetricTarget& target) const {
    if (auto allowed = requireLogin(); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto metric = m_metrics.getDefinition(id);
    if (!metric) {
        return std::unexpected(metric.error());
    }
    const auto date = targetDate(*metric, team, target);
    if (!date) {
        return std::unexpected(date.error());
    }
    auto players = m_players.list(team, data::ArchivedPlayers::Exclude);
    if (!players) {
        return std::unexpected(players.error());
    }
    const auto stored = m_metrics.listValues(id, target);
    if (!stored) {
        return std::unexpected(stored.error());
    }
    // The last one wins if an import has left two rows of a player.
    std::map<core::PlayerId, double> valueByPlayer;
    for (const core::MetricValue& value : *stored) {
        valueByPlayer.insert_or_assign(value.playerId, value.value);
    }
    MetricSheet sheet{std::move(*metric), target, *date, {}};
    sheet.rows.reserve(players->size());
    for (core::Player& player : *players) {
        MetricSheetRow row;
        if (const auto found = valueByPlayer.find(player.id); found != valueByPlayer.end()) {
            row.value = found->second;
        }
        row.player = std::move(player);
        sheet.rows.push_back(std::move(row));
    }
    return sheet;
}

core::Result<std::vector<MetricRowError>>
MetricService::saveValues(core::MetricDefinitionId id, core::TeamId team,
                          const core::MetricTarget& target,
                          std::span<const core::MetricValueDraft> values) {
    if (auto allowed = m_session.require(core::Permission::ManageMetrics); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto metric = m_metrics.getDefinition(id);
    if (!metric) {
        return std::unexpected(metric.error());
    }
    const auto date = targetDate(*metric, team, target);
    if (!date) {
        return std::unexpected(date.error());
    }
    // Archived players too: their rows are not on the sheet, but a value of theirs is not wrong.
    const auto players = m_players.list(team, data::ArchivedPlayers::Include);
    if (!players) {
        return std::unexpected(players.error());
    }
    std::set<core::PlayerId> teamPlayers;
    for (const core::Player& player : *players) {
        teamPlayers.insert(player.id);
    }

    std::vector<MetricRowError> errors;
    std::set<core::PlayerId> given;
    for (const core::MetricValueDraft& row : values) {
        if (!teamPlayers.contains(row.playerId)) {
            errors.push_back({row.playerId, error(core::ErrorCode::InvalidInput,
                                                  "the player is not of the team", "player_id")});
        } else if (!given.insert(row.playerId).second) {
            errors.push_back({row.playerId, error(core::ErrorCode::InvalidInput,
                                                  "the player is given twice", "player_id")});
        } else if (row.value) {
            if (auto valid = firstError(core::validateValue(*metric, *row.value)); !valid) {
                errors.push_back({row.playerId, valid.error()});
            }
        }
    }
    if (!errors.empty()) {
        return errors;
    }
    if (auto saved = m_metrics.saveValues(id, target, *date, values); !saved) {
        return std::unexpected(saved.error());
    }
    if (auto committed = transaction->commit(); !committed) {
        return std::unexpected(committed.error());
    }
    return errors;
}

core::Result<std::vector<PlayerMetricSummary>>
MetricService::playerSummary(core::PlayerId player) const {
    if (auto allowed = requireLogin(); !allowed) {
        return std::unexpected(allowed.error());
    }
    const auto definitions = m_metrics.listDefinitions();
    if (!definitions) {
        return std::unexpected(definitions.error());
    }
    const auto values = m_metrics.listPlayerValues(player);
    if (!values) {
        return std::unexpected(values.error());
    }
    const core::Date today = m_clock.today();
    const core::Date windowStart{std::chrono::sys_days{today} -
                                 std::chrono::days{core::kMetricWindowDays - 1}};

    std::vector<PlayerMetricSummary> result;
    for (const core::MetricDefinition& definition : *definitions) {
        // Oldest first, as the repository gives them; a planned match later than today waits.
        std::vector<const core::MetricValue*> history;
        for (const core::MetricValue& value : *values) {
            if (value.metricId == definition.id && value.date <= today) {
                history.push_back(&value);
            }
        }
        if (history.empty()) {
            continue;
        }
        PlayerMetricSummary summary;
        summary.metric = definition;
        summary.latest = history.back()->value;
        summary.latestDate = history.back()->date;
        if (history.size() > 1) {
            summary.previous = history[history.size() - 2]->value;
        }
        std::vector<double> inWindow;
        for (const core::MetricValue* value : history) {
            if (value->date >= windowStart) {
                inWindow.push_back(value->value);
            }
        }
        summary.window = core::aggregate(inWindow, definition.aggregation);
        summary.windowCount = static_cast<int>(inWindow.size());
        summary.trend = core::trendOf(summary.latest, summary.previous);
        result.push_back(std::move(summary));
    }
    return result;
}

core::Status MetricService::checkNamesFree(const core::MetricDefinitionDraft& draft,
                                           std::optional<core::MetricDefinitionId> except) const {
    const auto definitions = m_metrics.listDefinitions();
    if (!definitions) {
        return std::unexpected(definitions.error());
    }
    const auto taken = [&](const std::string& name) {
        const std::string key = core::metricNameKey(name);
        return !key.empty() &&
               std::ranges::any_of(*definitions, [&](const core::MetricDefinition& other) {
                   return other.id != except && (core::metricNameKey(other.namePl) == key ||
                                                 core::metricNameKey(other.nameEn) == key);
               });
    };
    if (taken(draft.namePl)) {
        return std::unexpected(
            error(core::ErrorCode::Conflict, "another metric has a similar name", "name_pl"));
    }
    if (taken(draft.nameEn)) {
        return std::unexpected(
            error(core::ErrorCode::Conflict, "another metric has a similar name", "name_en"));
    }
    return {};
}

core::Status MetricService::requireLogin() const {
    if (m_session.user()) {
        return {};
    }
    return std::unexpected(
        core::Error{core::ErrorCode::PermissionDenied, "nobody is logged in", {}});
}

core::Result<core::Date> MetricService::targetDate(const core::MetricDefinition& metric,
                                                   core::TeamId team,
                                                   const core::MetricTarget& target) const {
    if (core::scopeOf(target) != metric.scope) {
        return std::unexpected(error(core::ErrorCode::InvalidInput,
                                     "the entry does not fit the scope of the metric", "scope"));
    }
    if (const auto* match = std::get_if<core::MatchId>(&target)) {
        const auto stored = m_matches.get(*match);
        if (!stored) {
            return std::unexpected(stored.error());
        }
        if (stored->teamId != team) {
            return std::unexpected(
                error(core::ErrorCode::InvalidInput, "the match is of another team", "team"));
        }
        return stored->date;
    }
    if (const auto* session = std::get_if<core::TrainingSessionId>(&target)) {
        const auto stored = m_training.get(*session);
        if (!stored) {
            return std::unexpected(stored.error());
        }
        if (stored->teamId != team) {
            return std::unexpected(
                error(core::ErrorCode::InvalidInput, "the session is of another team", "team"));
        }
        return stored->date;
    }
    const core::Date day = std::get<core::Date>(target);
    if (!core::isSupportedDate(day)) {
        return std::unexpected(error(core::ErrorCode::InvalidInput, "unsupported date", "date"));
    }
    return day;
}

} // namespace fdss::services
