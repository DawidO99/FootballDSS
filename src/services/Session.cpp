#include "services/Session.hpp"

#include <format>
#include <utility>

namespace fdss::services {

void Session::start(core::StaffUser user) {
    m_user = std::move(user);
    ++m_serial;
}

void Session::clear() {
    if (m_user) {
        m_user.reset();
        ++m_serial;
    }
}

void Session::rename(std::string name) {
    if (m_user) {
        m_user->name = std::move(name);
    }
}

const std::optional<core::StaffUser>& Session::user() const { return m_user; }

int Session::serial() const { return m_serial; }

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
