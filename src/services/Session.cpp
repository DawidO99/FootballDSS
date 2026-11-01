#include "services/Session.hpp"

#include <format>
#include <utility>

namespace fdss::services {

void Session::start(core::StaffUser user) { m_user = std::move(user); }

void Session::clear() { m_user.reset(); }

const std::optional<core::StaffUser>& Session::user() const { return m_user; }

bool Session::isAllowed(core::Permission permission) const {
    return m_user && core::isAllowed(m_user->role, permission);
}

core::Status Session::require(core::Permission permission) const {
    if (isAllowed(permission)) {
        return {};
    }
    return std::unexpected(
        core::Error{core::ErrorCode::PermissionDenied,
                    std::format("the session user lacks permission {}", core::toString(permission)),
                    {}});
}

} // namespace fdss::services
