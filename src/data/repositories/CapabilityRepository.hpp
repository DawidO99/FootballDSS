#pragma once

#include "core/algorithms/Capabilities.hpp"
#include "core/domain/DateTypes.hpp"
#include "core/domain/Ids.hpp"
#include "core/util/Error.hpp"

namespace fdss::data {

class DatabaseManager;

// The counters of the capability model (SPEC section 2, P15 decision 3), one statement for all.
class CapabilityRepository {
public:
    explicit CapabilityRepository(DatabaseManager& db) : m_db(db) {}

    // RPE days count through `today`: present training attendance and appearances in finished
    // matches with an RPE.
    [[nodiscard]] core::Result<core::DataCounts> counts(core::TeamId team, core::Date today) const;

private:
    DatabaseManager& m_db;
};

} // namespace fdss::data
