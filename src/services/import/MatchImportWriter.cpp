#include "services/import/MatchImportWriter.hpp"

#include "core/domain/Validation.hpp"
#include "data/repositories/ImportLogRepository.hpp"
#include "data/repositories/MatchLiveRepository.hpp"
#include "data/repositories/MatchRepository.hpp"
#include "data/repositories/MetricRepository.hpp"
#include "data/repositories/OpponentRepository.hpp"
#include "services/ServiceSupport.hpp"
#include "services/import/ImportService.hpp"

#include <algorithm>
#include <optional>

namespace fdss::services {

core::Result<MatchSnapshot>
MatchImportWriter::snapshot(core::TeamId team, const core::MatchImport& import,
                            const core::ActivityMapping& mapping) const {
    MatchSnapshot result;
    auto opponents = m_opponents.list();
    if (!opponents) {
        return std::unexpected(opponents.error());
    }
    result.opponents = std::move(*opponents);
    if (import.matches.empty()) {
        return result;
    }
    const auto [first, last] = std::ranges::minmax(import.matches, {}, &core::MatchGroup::date);
    auto matches = m_matches.list(team, core::DateRange{first.date, last.date});
    if (!matches) {
        return std::unexpected(matches.error());
    }
    std::vector<core::MetricDefinitionId> metrics;
    for (const core::ColumnTarget& target : mapping.columns) {
        if (target.metric) {
            metrics.push_back(*target.metric);
        }
    }
    for (core::Match& match : *matches) {
        const bool inFile = std::ranges::any_of(import.matches, [&](const core::MatchGroup& group) {
            return group.date == match.date;
        });
        if (!inFile) {
            continue;
        }
        core::ExistingMatch existing;
        auto live = m_matchLive.find(match.id);
        if (!live) {
            return std::unexpected(live.error());
        }
        existing.recorded = live->has_value();
        auto appearances = m_matches.listAppearances(match.id);
        if (!appearances) {
            return std::unexpected(appearances.error());
        }
        existing.appearances = std::move(*appearances);
        for (const core::MetricDefinitionId metric : metrics) {
            auto values = m_metrics.listValues(metric, match.id);
            if (!values) {
                return std::unexpected(values.error());
            }
            std::ranges::move(*values, std::back_inserter(existing.values));
        }
        existing.match = std::move(match);
        result.matches.push_back(std::move(existing));
    }
    return result;
}

core::Status MatchImportWriter::write(core::TeamId team, const core::MatchPlan& plan,
                                      std::span<const core::PlayerId> newPlayers,
                                      core::ImportId importId, data::ImportReport& report,
                                      ActivityImportSummary& summary) {
    std::vector<std::optional<core::OpponentId>> opponents(plan.newOpponents.size());
    std::vector<std::optional<core::MatchId>> ids(plan.matches.size());
    for (std::size_t index = 0; index < plan.matches.size(); ++index) {
        const core::PlannedMatch& planned = plan.matches[index];
        if (planned.existing) {
            ids[index] = planned.existing;
            continue;
        }
        if (!planned.written) {
            continue;
        }
        core::MatchDraft draft = planned.draft;
        if (planned.newOpponent) {
            std::optional<core::OpponentId>& opponent = opponents[*planned.newOpponent];
            if (!opponent) {
                const core::OpponentDraft created{plan.newOpponents[*planned.newOpponent],
                                                  std::nullopt};
                if (auto valid = firstError(core::validate(created)); !valid) {
                    return valid;
                }
                auto id = m_opponents.create(created);
                if (!id) {
                    return std::unexpected(id.error());
                }
                opponent = *id;
                report.createdOpponents.push_back(*id);
                ++summary.opponentsCreated;
            }
            draft.opponentId = *opponent;
        }
        if (auto valid = firstError(core::validate(draft)); !valid) {
            return valid;
        }
        auto created = m_matches.create(team, draft, importId);
        if (!created) {
            return std::unexpected(created.error());
        }
        if (auto result = m_matches.setResult(*created, planned.goalsFor, planned.goalsAgainst,
                                              core::MatchStatus::Finished);
            !result) {
            return result;
        }
        ids[index] = *created;
        ++summary.matchesCreated;
    }

    for (const core::PlannedMatchRow& row : plan.rows) {
        if (row.action != core::ActivityAction::Create &&
            row.action != core::ActivityAction::Update) {
            continue;
        }
        const core::MatchId match = *ids[*row.match];
        const core::Date date = plan.matches[*row.match].draft.date;
        const core::PlayerId player =
            row.player.existing ? *row.player.existing : newPlayers[*row.player.newPlayer];
        if (row.appearance && (row.appearanceWrite == core::WriteKind::Create ||
                               row.appearanceWrite == core::WriteKind::Update)) {
            core::AppearanceDraft draft = *row.appearance;
            draft.playerId = player;
            if (auto valid = firstError(core::validate(draft)); !valid) {
                return valid;
            }
            const bool create = row.appearanceWrite == core::WriteKind::Create;
            auto written = create ? m_matches.insertAppearance(match, draft, importId)
                                  : m_matches.updateAppearance(match, draft);
            if (!written) {
                return written;
            }
            report.appearances.push_back(data::ImportedAppearance{
                match, draft, create ? std::nullopt : row.previousAppearance});
        }
        for (const core::PlannedValue& value : row.values) {
            if (value.write == core::WriteKind::Create) {
                if (auto inserted = m_metrics.insertValue(value.metric, match, date, player,
                                                          value.value, importId);
                    !inserted) {
                    return inserted;
                }
            } else if (value.write == core::WriteKind::Update) {
                auto stored = m_metrics.findValue(value.metric, match, player);
                if (!stored) {
                    return std::unexpected(stored.error());
                }
                if (!*stored) {
                    return std::unexpected(core::Error{core::ErrorCode::Conflict,
                                                       "the metric value is gone",
                                                       "import_matches_changed"});
                }
                if (auto updated = m_metrics.updateValue((*stored)->id, value.value); !updated) {
                    return updated;
                }
            } else {
                continue;
            }
            report.metricValues.push_back(data::ImportedMetricValue{
                value.metric, std::nullopt, match, player, value.value,
                value.write == core::WriteKind::Update ? value.previous : std::nullopt});
            ++summary.metricValues;
        }
    }
    return {};
}

} // namespace fdss::services
