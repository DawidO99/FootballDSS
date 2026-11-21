#pragma once

#include "core/algorithms/Capabilities.hpp"
#include "core/domain/Ids.hpp"
#include "core/util/Error.hpp"

#include <vector>

namespace fdss::data {
class CapabilityRepository;
} // namespace fdss::data

namespace fdss::services {

class Clock;
class Session;

// Which features the data of a team supports (SPEC section 2, packet P15). Needs ViewAnalysis,
// which every role has, and writes nothing. One small query on the calling thread.
class CapabilityService {
public:
    CapabilityService(data::CapabilityRepository& counts, Session& session, const Clock& clock);

    // The defaults of SPEC section 2; not editable in the UI (P15, out of scope).
    [[nodiscard]] const core::CapabilityThresholds& thresholds() const;

    [[nodiscard]] core::Result<std::vector<core::Capability>> evaluate(core::TeamId team,
                                                                       bool showAdvanced) const;

private:
    data::CapabilityRepository& m_counts;
    Session& m_session;
    const Clock& m_clock;
    core::CapabilityThresholds m_thresholds;
};

} // namespace fdss::services
