#pragma once

#include "core/domain/Permissions.hpp"
#include "core/domain/StaffUser.hpp"
#include "core/util/Error.hpp"

#include <optional>
#include <string>

namespace fdss::services {

// Who is logged in. Nothing is stored: every start of the app begins logged out.
class Session {
public:
    // A login, also of the user already logged in or over another user.
    void start(core::StaffUser user);
    void clear();
    // The same session under a new name. Does nothing when nobody is logged in.
    void rename(std::string name);

    [[nodiscard]] const std::optional<core::StaffUser>& user() const;
    // Changes with every login and every logout, so a viewmodel can tell that a session is another
    // one even when the user is the same. A rename keeps it.
    [[nodiscard]] int serial() const;
    // False when nobody is logged in.
    [[nodiscard]] bool isAllowed(core::Permission permission) const;
    // isAllowed() as a Status for the first line of a service method: PermissionDenied otherwise.
    [[nodiscard]] core::Status require(core::Permission permission) const;

private:
    std::optional<core::StaffUser> m_user;
    int m_serial = 0;
};

} // namespace fdss::services
