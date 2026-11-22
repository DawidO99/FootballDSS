#include "services/import/TrainingImportWriter.hpp"

#include "core/domain/Validation.hpp"
#include "data/repositories/ImportLogRepository.hpp"
#include "data/repositories/MetricRepository.hpp"
#include "data/repositories/TrainingRepository.hpp"
#include "services/ServiceSupport.hpp"
#include "services/import/ImportService.hpp"

#include <algorithm>
#include <optional>

namespace fdss::services {

namespace {

std::vector<core::MetricDefinitionId> metricsOf(const core::ActivityMapping& mapping) {
    std::vector<core::MetricDefinitionId> metrics;
    for (const core::ColumnTarget& target : mapping.columns) {
        if (target.metric) {
            metrics.push_back(*target.metric);
        }
    }
    return metrics;
}

} // namespace

core::Result<std::vector<core::ExistingSession>>
TrainingImportWriter::snapshot(core::TeamId team, const core::TrainingImport& import,
                               const core::ActivityMapping& mapping) const {
    std::vector<core::ExistingSession> result;
    if (import.sessions.empty()) {
        return result;
    }
    const auto [first, last] =
        std::ranges::minmax(import.sessions, {}, &core::TrainingSessionGroup::date);
    auto sessions = m_training.list(team, core::DateRange{first.date, last.date});
    if (!sessions) {
        return std::unexpected(sessions.error());
    }
    const std::vector<core::MetricDefinitionId> metrics = metricsOf(mapping);
    for (core::TrainingSession& session : *sessions) {
        const bool inFile = std::ranges::any_of(
            import.sessions, [&](const auto& group) { return group.date == session.date; });
        if (!inFile) {
            continue;
        }
        core::ExistingSession existing;
        auto attendance = m_training.listAttendance(session.id);
        if (!attendance) {
            return std::unexpected(attendance.error());
        }
        existing.attendance = std::move(*attendance);
        for (const core::MetricDefinitionId metric : metrics) {
            auto values = m_metrics.listValues(metric, session.id);
            if (!values) {
                return std::unexpected(values.error());
            }
            std::ranges::move(*values, std::back_inserter(existing.values));
        }
        existing.session = std::move(session);
        result.push_back(std::move(existing));
    }
    return result;
}

core::Status TrainingImportWriter::write(core::TeamId team, const core::TrainingPlan& plan,
                                         std::span<const core::PlayerId> newPlayers,
                                         core::ImportId importId, data::ImportReport& report,
                                         ActivityImportSummary& summary) {
    std::vector<std::optional<core::TrainingSessionId>> ids(plan.sessions.size());
    for (std::size_t index = 0; index < plan.sessions.size(); ++index) {
        const core::PlannedSession& session = plan.sessions[index];
        if (session.existing) {
            ids[index] = session.existing;
            continue;
        }
        if (!session.written) {
            continue;
        }
        if (auto valid = firstError(core::validate(session.draft)); !valid) {
            return valid;
        }
        auto created = m_training.create(team, session.draft, importId);
        if (!created) {
            return std::unexpected(created.error());
        }
        ids[index] = *created;
        ++summary.sessionsCreated;
    }

    for (const core::PlannedTrainingRow& row : plan.rows) {
        if (row.action != core::ActivityAction::Create &&
            row.action != core::ActivityAction::Update) {
            continue;
        }
        const core::TrainingSessionId session = *ids[*row.session];
        const core::Date date = plan.sessions[*row.session].draft.date;
        const core::PlayerId player =
            row.player.existing ? *row.player.existing : newPlayers[*row.player.newPlayer];
        if (row.attendance && (row.attendanceWrite == core::WriteKind::Create ||
                               row.attendanceWrite == core::WriteKind::Update)) {
            core::AttendanceDraft draft = *row.attendance;
            draft.playerId = player;
            if (auto valid = firstError(core::validate(draft)); !valid) {
                return valid;
            }
            const bool create = row.attendanceWrite == core::WriteKind::Create;
            auto written = create ? m_training.insertAttendance(session, draft, importId)
                                  : m_training.updateAttendance(session, draft);
            if (!written) {
                return written;
            }
            report.attendance.push_back(data::ImportedAttendance{
                session, draft, create ? std::nullopt : row.previousAttendance});
        }
        for (const core::PlannedValue& value : row.values) {
            if (value.write == core::WriteKind::Create) {
                if (auto inserted = m_metrics.insertValue(value.metric, session, date, player,
                                                          value.value, importId);
                    !inserted) {
                    return inserted;
                }
            } else if (value.write == core::WriteKind::Update) {
                auto stored = m_metrics.findValue(value.metric, session, player);
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
                value.metric, session, std::nullopt, player, value.value,
                value.write == core::WriteKind::Update ? value.previous : std::nullopt});
            ++summary.metricValues;
        }
    }
    return {};
}

} // namespace fdss::services
