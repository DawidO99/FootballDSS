#include "core/algorithms/Capabilities.hpp"

#include <algorithm>

namespace fdss::core {

namespace {

Capability counted(CapabilityKind kind, int have, int need) {
    Capability capability;
    capability.kind = kind;
    capability.have = have;
    capability.need = need;
    capability.state = have >= need ? CapabilityState::Available : CapabilityState::Locked;
    return capability;
}

} // namespace

std::vector<Capability> evaluateCapabilities(const DataCounts& counts,
                                             const CapabilityThresholds& thresholds,
                                             bool showAdvanced) {
    std::vector<Capability> result;
    result.reserve(kCapabilityKinds.size());
    for (const CapabilityKind kind : kCapabilityKinds) {
        switch (kind) {
        case CapabilityKind::Squad:
            result.push_back(counted(kind, counts.activePlayers, thresholds.minPlayers));
            break;
        case CapabilityKind::Matches:
            result.push_back(counted(kind, counts.finishedMatches, thresholds.minFinishedMatches));
            break;
        case CapabilityKind::Load: {
            Capability load = counted(kind, counts.rpeDays, thresholds.minRpeDays);
            if (load.state == CapabilityState::Available &&
                counts.daysSinceFirstRpe < thresholds.fullLoadDays) {
                load.daysToFullLoad =
                    thresholds.fullLoadDays - std::max(counts.daysSinceFirstRpe, 0);
            }
            result.push_back(load);
            break;
        }
        case CapabilityKind::Ratings:
            result.push_back(counted(kind, counts.ratingEntries, thresholds.minRatingEntries));
            break;
        case CapabilityKind::Opponents:
            result.push_back(
                counted(kind, counts.matchesWithResult, thresholds.minMatchesWithResult));
            break;
        case CapabilityKind::EventData: {
            Capability events = counted(kind, counts.eventFiles, thresholds.minEventFiles);
            if (events.state == CapabilityState::Locked && !showAdvanced) {
                events.state = CapabilityState::Hidden;
            }
            result.push_back(events);
            break;
        }
        }
    }
    return result;
}

} // namespace fdss::core
