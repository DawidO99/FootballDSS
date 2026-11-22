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
    // A login, also of the user already logged in or over another user. mustChangePin: the user
    // logged in with a PIN a Coach chose and has to set their own first (P19 decision 5).
    void start(core::StaffUser user, bool mustChangePin = false);
    void clear();
    // The same session under a new name. Does nothing when nobody is logged in.
    void rename(std::string name);

    [[nodiscard]] const std::optional<core::StaffUser>& user() const;
    // Changes with every login and every logout, so a viewmodel can tell that a session is another
    // one even when the user is the same. A rename keeps it.
    [[nodiscard]] int serial() const;
    // From a login with a PIN a Coach chose until pinChanged(). Nothing is allowed meanwhile, so
    // pinChanged() counts as a new session for the serial: what was read without rights is read
    // again.
    [[nodiscard]] bool mustChangePin() const;
    void pinChanged();
    // False when nobody is logged in, and while the user must change their PIN.
    [[nodiscard]] bool isAllowed(core::Permission permission) const;
    // isAllowed() as a Status for the first line of a service method: PermissionDenied otherwise.
    [[nodiscard]] core::Status require(core::Permission permission) const;

private:
    std::optional<core::StaffUser> m_user;
    bool m_mustChangePin = false;
    int m_serial = 0;
};

} // namespace fdss::services
