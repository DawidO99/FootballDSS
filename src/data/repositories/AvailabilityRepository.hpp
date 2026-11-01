#pragma once

#include "core/domain/Availability.hpp"
#include "core/util/Error.hpp"

#include <vector>

namespace fdss::data {

class DatabaseManager;

// Returns the description to every caller: it does not know the logged-in user, so hiding it from a
// role without ViewAvailabilityDetails is service work.
class AvailabilityRepository {
public:
    explicit AvailabilityRepository(DatabaseManager& db) : m_db(db) {}

    // Newest first.
    [[nodiscard]] core::Result<std::vector<core::Availability>>
    listByPlayer(core::PlayerId player) const;
    // The entries for which core::isActiveOn() holds, for players of the team who are not archived
    // on that date.
    [[nodiscard]] core::Result<std::vector<core::Availability>> listActiveOn(core::TeamId team,
                                                                             core::Date date) const;
    [[nodiscard]] core::Result<core::AvailabilityId> create(core::PlayerId player,
                                                            const core::AvailabilityDraft& draft);
    // draft.description: nullopt keeps the stored text, an empty text clears it.
    [[nodiscard]] core::Status update(core::AvailabilityId id,
                                      const core::AvailabilityDraft& draft);
    // actualTo is the first available day.
    [[nodiscard]] core::Status close(core::AvailabilityId id, core::Date actualTo);
    [[nodiscard]] core::Status remove(core::AvailabilityId id);

private:
    DatabaseManager& m_db;
};

} // namespace fdss::data
