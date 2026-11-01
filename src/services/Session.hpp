#pragma once

#include "core/domain/Permissions.hpp"
#include "core/domain/StaffUser.hpp"
#include "core/util/Error.hpp"

#include <optional>

namespace fdss::services {

// Who is logged in. Nothing is stored: every start of the app begins logged out.
class Session {
public:
    void start(core::StaffUser user);
    void clear();

    [[nodiscard]] const std::optional<core::StaffUser>& user() const;
    // False when nobody is logged in.
    [[nodiscard]] bool isAllowed(core::Permission permission) const;
    // isAllowed() as a Status for the first line of a service method: PermissionDenied otherwise.
    [[nodiscard]] core::Status require(core::Permission permission) const;

private:
    std::optional<core::StaffUser> m_user;
};

} // namespace fdss::services
