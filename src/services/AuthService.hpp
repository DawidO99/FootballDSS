#pragma once

#include "core/domain/StaffUser.hpp"
#include "core/util/Error.hpp"

#include <chrono>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace fdss::data {
class DatabaseManager;
class StaffRepository;
} // namespace fdss::data

namespace fdss::services {

class Clock;
class Session;

inline constexpr int kDefaultPinIterations = 120'000;
inline constexpr int kMaxFailedAttempts = 5;
inline constexpr std::chrono::seconds kLockoutDuration{30};

enum class PinCheckStatus { Accepted, WrongPin, LockedOut };

// A wrong PIN and a lockout are answers, not failures: Error stays for what really went wrong.
struct PinCheck {
    PinCheckStatus status = PinCheckStatus::WrongPin;
    int remainingAttempts = 0;         // WrongPin: attempts left before the lockout
    std::chrono::seconds lockedFor{0}; // LockedOut: time left
};

// Staff PINs per ADR-007: PBKDF2-HMAC-SHA256, random salt, parameters stored per user. Runs on the
// main thread: one hash takes about 0.13 s, and the attempt counter is written on the main-thread
// connection anyway (P07a Decisions).
class AuthService {
public:
    // iterations: for new hashes. A stored PIN is checked with the iterations stored next to it.
    AuthService(data::DatabaseManager& db, data::StaffRepository& staff, Session& session,
                const Clock& clock, int iterations);

    // 4 to 8 ASCII digits.
    [[nodiscard]] static bool isValidPin(std::string_view pin);
    // A new salt every time. Malformed PIN: InvalidInput with field "pin".
    [[nodiscard]] core::Result<core::PinHash> hashPin(std::string_view pin) const;

    // No session needed: the profile picker comes before the login.
    [[nodiscard]] core::Result<std::vector<core::StaffUser>> listActiveStaff() const;
    // Zero when the user is not locked. Lets the picker show a lockout that survived a restart.
    [[nodiscard]] core::Result<std::chrono::seconds> lockRemaining(core::StaffUserId id) const;
    // Accepted starts the session. Inactive user: PermissionDenied. A malformed PIN is not counted
    // as an attempt: InvalidInput with field "pin".
    [[nodiscard]] core::Result<PinCheck> login(core::StaffUserId id, std::string_view pin);
    void logout();

    // The PIN of a user who logged in with one a Coach chose (Session::mustChangePin()); the login
    // has just checked the current one. Otherwise PermissionDenied. Malformed: InvalidInput with
    // field "pin"; the same PIN as before: InvalidInput with field "pin_same". Ends the
    // must-change state of the session.
    [[nodiscard]] core::Status setRequiredPin(std::string_view newPin);

    // Needs a logged-in user, no permission. A wrong current PIN counts towards the lockout, so
    // this is no way around the limit of attempts.
    [[nodiscard]] core::Result<PinCheck> changeOwnPin(std::string_view currentPin,
                                                      std::string_view newPin);

    // The methods below need ManageStaff of the session user, otherwise PermissionDenied. A user
    // added, or whose PIN was reset by someone else, must change the PIN at the next login.
    //
    // Taking the last active Coach away gives Conflict with field "last_coach". Changing the role
    // or deactivating the session user ends the session, so nothing keeps running on old rights;
    // renaming that user only updates it.
    [[nodiscard]] core::Result<std::vector<core::StaffUser>> listAllStaff() const;
    [[nodiscard]] core::Result<core::StaffUserId> addStaff(const core::StaffUserDraft& draft,
                                                           std::string_view pin);
    [[nodiscard]] core::Status updateStaff(core::StaffUserId id, const core::StaffUserDraft& draft);
    // Also clears the attempt counter and the lockout. Resetting one's own PIN is no reason to
    // change it again.
    [[nodiscard]] core::Status resetPin(core::StaffUserId id, std::string_view newPin);
    [[nodiscard]] core::Status deactivate(core::StaffUserId id);
    [[nodiscard]] core::Status reactivate(core::StaffUserId id);

private:
    using StaffChange = std::function<core::StaffUserDraft(const core::StaffUser&)>;

    // One transaction around the last Coach check and the write, then the session rule above.
    [[nodiscard]] core::Status changeStaff(core::StaffUserId id, const StaffChange& change);

    data::DatabaseManager& m_db;
    data::StaffRepository& m_staff;
    Session& m_session;
    const Clock& m_clock;
    int m_iterations;
};

} // namespace fdss::services
