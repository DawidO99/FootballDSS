#include "services/import/ActivityUndo.hpp"

#include "data/repositories/ImportLogRepository.hpp"
#include "data/repositories/MatchRepository.hpp"
#include "data/repositories/MetricRepository.hpp"
#include "data/repositories/TrainingRepository.hpp"
#include "services/import/ImportService.hpp"

#include <optional>

namespace fdss::services {

namespace {

bool sameAttendance(const core::AttendanceDraft& left, const core::AttendanceDraft& right) {
    return left.status == right.status && left.rpe == right.rpe &&
           left.minutesOverride == right.minutesOverride;
}

bool sameAppearance(const core::AppearanceDraft& left, const core::AppearanceDraft& right) {
    return left.started == right.started && left.minuteOn == right.minuteOn &&
           left.minuteOff == right.minuteOff && left.rating == right.rating &&
           left.rpe == right.rpe;
}

core::MetricTarget targetOf(const data::ImportedMetricValue& row) {
    if (row.session) {
        return *row.session;
    }
    return *row.match;
}

// The stored value of a row, nullopt when it is gone.
core::Result<std::optional<core::AttendanceDraft>>
currentAttendance(const ActivityRowStores& stores, const data::ImportedAttendance& row) {
    auto stored = stores.training.findAttendance(row.session, row.written.playerId);
    if (!stored) {
        return std::unexpected(stored.error());
    }
    if (!*stored) {
        return std::optional<core::AttendanceDraft>{};
    }
    return std::optional<core::AttendanceDraft>(
        static_cast<const core::AttendanceDraft&>(**stored));
}

core::Result<std::optional<core::AppearanceDraft>>
currentAppearance(const ActivityRowStores& stores, const data::ImportedAppearance& row) {
    auto stored = stores.matches.findAppearance(row.match, row.written.playerId);
    if (!stored) {
        return std::unexpected(stored.error());
    }
    if (!*stored) {
        return std::optional<core::AppearanceDraft>{};
    }
    return std::optional<core::AppearanceDraft>(
        static_cast<const core::AppearanceDraft&>(**stored));
}

} // namespace

core::Status countChangedRows(const data::ImportReport& report, const ActivityRowStores& stores,
                              ImportUndoPreview& preview) {
    for (const data::ImportedAttendance& row : report.attendance) {
        if (row.previous) {
            continue;
        }
        auto current = currentAttendance(stores, row);
        if (!current) {
            return std::unexpected(current.error());
        }
        if (*current && !sameAttendance(**current, row.written)) {
            ++preview.entries;
        }
    }
    for (const data::ImportedAppearance& row : report.appearances) {
        if (row.previous) {
            continue;
        }
        auto current = currentAppearance(stores, row);
        if (!current) {
            return std::unexpected(current.error());
        }
        if (*current && !sameAppearance(**current, row.written)) {
            ++preview.entries;
        }
    }
    for (const data::ImportedMetricValue& row : report.metricValues) {
        if (row.previous) {
            continue;
        }
        auto current = stores.metrics.findValue(row.metric, targetOf(row), row.player);
        if (!current) {
            return std::unexpected(current.error());
        }
        if (*current && (*current)->value != row.written) {
            ++preview.values;
        }
    }
    return {};
}

core::Status restoreOverwrittenRows(const data::ImportReport& report,
                                    const ActivityRowStores& stores, ImportUndoSummary& summary) {
    for (const data::ImportedAttendance& row : report.attendance) {
        if (!row.previous) {
            continue;
        }
        auto current = currentAttendance(stores, row);
        if (!current) {
            return std::unexpected(current.error());
        }
        if (!*current || !sameAttendance(**current, row.written)) {
            ++summary.notReverted;
            continue;
        }
        if (auto restored = stores.training.updateAttendance(row.session, *row.previous);
            !restored) {
            return restored;
        }
        ++summary.rowsRestored;
    }
    for (const data::ImportedAppearance& row : report.appearances) {
        if (!row.previous) {
            continue;
        }
        auto current = currentAppearance(stores, row);
        if (!current) {
            return std::unexpected(current.error());
        }
        if (!*current || !sameAppearance(**current, row.written)) {
            ++summary.notReverted;
            continue;
        }
        if (auto restored = stores.matches.updateAppearance(row.match, *row.previous); !restored) {
            return restored;
        }
        ++summary.rowsRestored;
    }
    for (const data::ImportedMetricValue& row : report.metricValues) {
        if (!row.previous) {
            continue;
        }
        auto current = stores.metrics.findValue(row.metric, targetOf(row), row.player);
        if (!current) {
            return std::unexpected(current.error());
        }
        if (!*current || (*current)->value != row.written) {
            ++summary.notReverted;
            continue;
        }
        if (auto restored = stores.metrics.updateValue((*current)->id, *row.previous); !restored) {
            return restored;
        }
        ++summary.rowsRestored;
    }
    return {};
}

} // namespace fdss::services
