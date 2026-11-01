#include "services/AuthService.hpp"

#include "core/domain/Permissions.hpp"
#include "core/domain/Validation.hpp"
#include "data/db/DatabaseManager.hpp"
#include "data/db/Transaction.hpp"
#include "data/repositories/StaffRepository.hpp"
#include "services/Clock.hpp"
#include "services/ServiceSupport.hpp"
#include "services/Session.hpp"

#include <QByteArray>
#include <QCryptographicHash>
#include <QPasswordDigestor>
#include <QRandomGenerator>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace fdss::services {

namespace {

constexpr std::size_t kMinPinLength = 4;
constexpr std::size_t kMaxPinLength = 8;
constexpr std::size_t kSaltBytes = 16;
constexpr std::size_t kKeyBytes = 32;

std::vector<std::uint8_t> deriveKey(std::string_view pin, std::span<const std::uint8_t> salt,
                                    int iterations) {
    const QByteArray key = QPasswordDigestor::deriveKeyPbkdf2(
        QCryptographicHash::Sha256, QByteArray(pin.data(), static_cast<qsizetype>(pin.size())),
        QByteArray(reinterpret_cast<const char*>(salt.data()), static_cast<qsizetype>(salt.size())),
        iterations, kKeyBytes);
    return {key.begin(), key.end()};
}

// Visits every byte whatever it finds. An empty stored hash, possible only in a damaged or edited
// file, matches nothing.
bool equalInConstantTime(std::span<const std::uint8_t> left, std::span<const std::uint8_t> right) {
    if (left.empty() || left.size() != right.size()) {
        return false;
    }
    std::uint8_t difference = 0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        difference |= static_cast<std::uint8_t>(left[i] ^ right[i]);
    }
    return difference == 0;
}

// Time left of the lockout at `now`; zero once it is over. More than a whole lockout left means
// that the system clock was moved back: the stored end says nothing then, and waiting for it could
// lock the only Coach out for as long as the clock was off.
std::chrono::seconds lockLeft(const core::StaffCredentials& credentials, core::Timestamp now) {
    if (!credentials.lockedUntil || *credentials.lockedUntil <= now) {
        return std::chrono::seconds::zero();
    }
    const std::chrono::seconds left = *credentials.lockedUntil - now;
    return left > kLockoutDuration ? std::chrono::seconds::zero() : left;
}

core::Error malformedPin() {
    return core::Error{core::ErrorCode::InvalidInput, "a PIN has 4 to 8 digits", "pin"};
}

// The answer to a PIN and the login state it leaves behind.
struct PinAttempt {
    PinCheck check;
    bool changesLoginState = false;
    int failedAttempts = 0;
    std::optional<core::Timestamp> lockedUntil;
};

// Reads only. The caller stores the result in its own transaction, so the write lock is not held
// for the length of a hash.
core::Result<PinAttempt> evaluatePin(const data::StaffRepository& staff, core::StaffUserId id,
                                     std::string_view pin, core::Timestamp now) {
    if (!AuthService::isValidPin(pin)) {
        return std::unexpected(malformedPin());
    }
    const auto credentials = staff.credentials(id);
    if (!credentials) {
        return std::unexpected(credentials.error());
    }
    // Checked first, so a locked account costs no hash and an attempt does not extend the lockout.
    if (const std::chrono::seconds left = lockLeft(*credentials, now); left.count() > 0) {
        return PinAttempt{PinCheck{PinCheckStatus::LockedOut, 0, left}, false, 0, std::nullopt};
    }

    const std::vector<std::uint8_t> key =
        deriveKey(pin, credentials->salt, credentials->iterations);
    if (equalInConstantTime(credentials->hash, key)) {
        const bool hadFailures = credentials->failedAttempts != 0 || credentials->lockedUntil;
        return PinAttempt{PinCheck{PinCheckStatus::Accepted, 0, std::chrono::seconds::zero()},
                          hadFailures, 0, std::nullopt};
    }

    // A lockout that is over gives a fresh set of attempts.
    const int failed = credentials->lockedUntil ? 1 : credentials->failedAttempts + 1;
    if (failed >= kMaxFailedAttempts) {
        return PinAttempt{PinCheck{PinCheckStatus::LockedOut, 0, kLockoutDuration}, true, failed,
                          now + kLockoutDuration};
    }
    return PinAttempt{PinCheck{PinCheckStatus::WrongPin, kMaxFailedAttempts - failed,
                               std::chrono::seconds::zero()},
                      true, failed, std::nullopt};
}

core::Status storeLoginState(data::StaffRepository& staff, core::StaffUserId id,
                             const PinAttempt& attempt) {
    if (!attempt.changesLoginState) {
        return {};
    }
    return staff.setLoginState(id, attempt.failedAttempts, attempt.lockedUntil);
}

} // namespace

AuthService::AuthService(data::DatabaseManager& db, data::StaffRepository& staff, Session& session,
                         const Clock& clock, int iterations)
    : m_db(db), m_staff(staff), m_session(session), m_clock(clock), m_iterations(iterations) {}

bool AuthService::isValidPin(std::string_view pin) {
    return pin.size() >= kMinPinLength && pin.size() <= kMaxPinLength &&
           std::ranges::all_of(pin, [](char digit) { return digit >= '0' && digit <= '9'; });
}

core::Result<core::PinHash> AuthService::hashPin(std::string_view pin) const {
    if (!isValidPin(pin)) {
        return std::unexpected(malformedPin());
    }
    core::PinHash result;
    result.salt.resize(kSaltBytes);
    std::ranges::generate(result.salt, [] {
        return static_cast<std::uint8_t>(QRandomGenerator::system()->generate());
    });
    result.iterations = m_iterations;
    result.hash = deriveKey(pin, result.salt, m_iterations);
    if (result.hash.size() != kKeyBytes) {
        return std::unexpected(
            core::Error{core::ErrorCode::Internal, "PBKDF2 did not return a key", {}});
    }
    return result;
}

core::Result<std::vector<core::StaffUser>> AuthService::listActiveStaff() const {
    return m_staff.listActive();
}

core::Result<std::chrono::seconds> AuthService::lockRemaining(core::StaffUserId id) const {
    const auto credentials = m_staff.credentials(id);
    if (!credentials) {
        return std::unexpected(credentials.error());
    }
    return lockLeft(*credentials, m_clock.now());
}

core::Result<PinCheck> AuthService::login(core::StaffUserId id, std::string_view pin) {
    auto user = m_staff.get(id);
    if (!user) {
        return std::unexpected(user.error());
    }
    if (!user->active) {
        return std::unexpected(
            core::Error{core::ErrorCode::PermissionDenied, "a deactivated user cannot log in", {}});
    }
    const auto attempt = evaluatePin(m_staff, id, pin, m_clock.now());
    if (!attempt) {
        return std::unexpected(attempt.error());
    }
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    if (auto status = storeLoginState(m_staff, id, *attempt); !status) {
        return std::unexpected(status.error());
    }
    if (auto status = transaction->commit(); !status) {
        return std::unexpected(status.error());
    }
    if (attempt->check.status == PinCheckStatus::Accepted) {
        m_session.start(std::move(*user));
    }
    return attempt->check;
}

void AuthService::logout() { m_session.clear(); }

core::Result<PinCheck> AuthService::changeOwnPin(std::string_view currentPin,
                                                 std::string_view newPin) {
    if (!m_session.user()) {
        return std::unexpected(
            core::Error{core::ErrorCode::PermissionDenied, "nobody is logged in", {}});
    }
    if (!isValidPin(newPin)) {
        return std::unexpected(malformedPin());
    }
    const core::StaffUserId id = m_session.user()->id;
    const auto attempt = evaluatePin(m_staff, id, currentPin, m_clock.now());
    if (!attempt) {
        return std::unexpected(attempt.error());
    }
    const bool accepted = attempt->check.status == PinCheckStatus::Accepted;
    core::PinHash pin;
    if (accepted) {
        auto hash = hashPin(newPin);
        if (!hash) {
            return std::unexpected(hash.error());
        }
        pin = std::move(*hash);
    }

    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    if (auto status = storeLoginState(m_staff, id, *attempt); !status) {
        return std::unexpected(status.error());
    }
    if (accepted) {
        if (auto status = m_staff.setPin(id, pin); !status) {
            return std::unexpected(status.error());
        }
    }
    if (auto status = transaction->commit(); !status) {
        return std::unexpected(status.error());
    }
    return attempt->check;
}

core::Result<std::vector<core::StaffUser>> AuthService::listAllStaff() const {
    if (auto allowed = m_session.require(core::Permission::ManageStaff); !allowed) {
        return std::unexpected(allowed.error());
    }
    return m_staff.listAll();
}

core::Result<core::StaffUserId> AuthService::addStaff(const core::StaffUserDraft& draft,
                                                      std::string_view pin) {
    if (auto allowed = m_session.require(core::Permission::ManageStaff); !allowed) {
        return std::unexpected(allowed.error());
    }
    if (auto valid = firstError(core::validate(draft)); !valid) {
        return std::unexpected(valid.error());
    }
    const auto hash = hashPin(pin);
    if (!hash) {
        return std::unexpected(hash.error());
    }
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto id = m_staff.create(draft, *hash);
    if (!id) {
        return id;
    }
    if (auto status = transaction->commit(); !status) {
        return std::unexpected(status.error());
    }
    return id;
}

core::Status AuthService::updateStaff(core::StaffUserId id, const core::StaffUserDraft& draft) {
    if (auto allowed = m_session.require(core::Permission::ManageStaff); !allowed) {
        return allowed;
    }
    if (auto valid = firstError(core::validate(draft)); !valid) {
        return valid;
    }
    return changeStaff(id, [&draft](const core::StaffUser&) { return draft; });
}

core::Status AuthService::resetPin(core::StaffUserId id, std::string_view newPin) {
    if (auto allowed = m_session.require(core::Permission::ManageStaff); !allowed) {
        return allowed;
    }
    const auto pin = hashPin(newPin);
    if (!pin) {
        return std::unexpected(pin.error());
    }
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    if (auto status = m_staff.setPin(id, *pin); !status) {
        return status;
    }
    if (auto status = m_staff.setLoginState(id, 0, std::nullopt); !status) {
        return status;
    }
    return transaction->commit();
}

core::Status AuthService::deactivate(core::StaffUserId id) {
    if (auto allowed = m_session.require(core::Permission::ManageStaff); !allowed) {
        return allowed;
    }
    return changeStaff(id, [](const core::StaffUser& user) {
        core::StaffUserDraft draft = user;
        draft.active = false;
        return draft;
    });
}

core::Status AuthService::reactivate(core::StaffUserId id) {
    if (auto allowed = m_session.require(core::Permission::ManageStaff); !allowed) {
        return allowed;
    }
    return changeStaff(id, [](const core::StaffUser& user) {
        core::StaffUserDraft draft = user;
        draft.active = true;
        return draft;
    });
}

core::Status AuthService::changeStaff(core::StaffUserId id, const StaffChange& change) {
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto current = m_staff.get(id);
    if (!current) {
        return std::unexpected(current.error());
    }
    const core::StaffUserDraft draft = change(*current);

    const bool isActiveCoach = current->active && current->role == core::Role::Coach;
    const bool staysActiveCoach = draft.active && draft.role == core::Role::Coach;
    if (isActiveCoach && !staysActiveCoach) {
        const auto coaches = m_staff.countActiveCoaches();
        if (!coaches) {
            return std::unexpected(coaches.error());
        }
        if (*coaches <= 1) {
            return std::unexpected(core::Error{
                core::ErrorCode::Conflict, "the last active coach has to stay one", "last_coach"});
        }
    }
    if (auto status = m_staff.update(id, draft); !status) {
        return status;
    }
    if (auto status = transaction->commit(); !status) {
        return status;
    }

    const auto& sessionUser = m_session.user();
    if (sessionUser && sessionUser->id == id) {
        if (!draft.active || draft.role != sessionUser->role) {
            m_session.clear();
        } else {
            core::StaffUser renamed = *sessionUser;
            renamed.name = core::trimmed(draft.name);
            m_session.start(std::move(renamed));
        }
    }
    return {};
}

} // namespace fdss::services
