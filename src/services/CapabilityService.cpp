#include "services/CapabilityService.hpp"

#include "core/domain/Permissions.hpp"
#include "data/repositories/CapabilityRepository.hpp"
#include "services/Clock.hpp"
#include "services/Session.hpp"

namespace fdss::services {

CapabilityService::CapabilityService(data::CapabilityRepository& counts, Session& session,
                                     const Clock& clock)
    : m_counts(counts), m_session(session), m_clock(clock) {}

const core::CapabilityThresholds& CapabilityService::thresholds() const { return m_thresholds; }

core::Result<std::vector<core::Capability>> CapabilityService::evaluate(core::TeamId team,
                                                                        bool showAdvanced) const {
    if (auto allowed = m_session.require(core::Permission::ViewAnalysis); !allowed) {
        return std::unexpected(allowed.error());
    }
    const auto counts = m_counts.counts(team, m_clock.today());
    if (!counts) {
        return std::unexpected(counts.error());
    }
    return core::evaluateCapabilities(*counts, m_thresholds, showAdvanced);
}

} // namespace fdss::services
