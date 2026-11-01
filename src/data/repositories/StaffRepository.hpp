#pragma once

#include "core/domain/StaffUser.hpp"
#include "core/util/Error.hpp"

#include <optional>
#include <vector>

namespace fdss::data {

class DatabaseManager;

// Stores the PIN bytes only; hashing and the lockout rules live in the services (ADR-007).
class StaffRepository {
public:
    explicit StaffRepository(DatabaseManager& db) : m_db(db) {}

    [[nodiscard]] core::Result<std::vector<core::StaffUser>> listActive() const;
    [[nodiscard]] core::Result<std::vector<core::StaffUser>> listAll() const;
    [[nodiscard]] core::Result<core::StaffUser> get(core::StaffUserId id) const;
    [[nodiscard]] core::Result<core::StaffUserId> create(const core::StaffUserDraft& draft,
                                                         const core::PinHash& pin);
    // Name, role and active. Staff users are deactivated, never deleted.
    [[nodiscard]] core::Status update(core::StaffUserId id, const core::StaffUserDraft& draft);

    [[nodiscard]] core::Result<core::StaffCredentials> credentials(core::StaffUserId id) const;
    [[nodiscard]] core::Status setPin(core::StaffUserId id, const core::PinHash& pin);
    [[nodiscard]] core::Status setLoginState(core::StaffUserId id, int failedAttempts,
                                             std::optional<core::Timestamp> lockedUntil);

    [[nodiscard]] core::Result<int> countActiveCoaches() const;

private:
    DatabaseManager& m_db;
};

} // namespace fdss::data
