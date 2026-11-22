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

// What an attempt needs before its transaction: the key of the PIN, or, for a locked account, the
// time left, with nothing hashed.
struct PreparedAttempt {
    std::vector<std::uint8_t> key;
    std::chrono::seconds lockedFor{0};
    bool mustChangePin = false;
};

// Outside any transaction, so the write lock is not held for the length of a hash. Only the salt
// and the iterations are used from this read: recordAttempt() reads the counter and the lockout
// again.
core::Result<PreparedAttempt> prepareAttempt(const data::StaffRepository& staff, const Clock& clock,
                                             core::StaffUserId id, std::string_view pin) {
    if (!AuthService::isValidPin(pin)) {
        return std::unexpected(malformedPin());
    }
    const auto credentials = staff.credentials(id);
    if (!credentials) {
        return std::unexpected(credentials.error());
    }
    if (const std::chrono::seconds left = lockLeft(*credentials, clock.now()); left.count() > 0) {
        return PreparedAttempt{{}, left, credentials->mustChangePin};
    }
    return PreparedAttempt{deriveKey(pin, credentials->salt, credentials->iterations),
                           std::chrono::seconds::zero(), credentials->mustChangePin};
}

// Inside the transaction of the caller, whose BEGIN IMMEDIATE serializes the attempts of all
// connections: the counter and the lockout are read and written under one lock, so no attempt gets
// lost. A PIN changed since the hash simply does not match, and an attempt does not extend a
// lockout.
core::Result<PinCheck> recordAttempt(data::StaffRepository& staff, const Clock& clock,
                                     core::StaffUserId id, std::span<const std::uint8_t> key) {
    const auto credentials = staff.credentials(id);
    if (!credentials) {
        return std::unexpected(credentials.error());
    }
    const core::Timestamp now = clock.now();
    if (const std::chrono::seconds left = lockLeft(*credentials, now); left.count() > 0) {
        return PinCheck{PinCheckStatus::LockedOut, 0, left};
    }

    if (equalInConstantTime(credentials->hash, key)) {
        if (credentials->failedAttempts != 0 || credentials->lockedUntil) {
            if (auto status = staff.setLoginState(id, 0, std::nullopt); !status) {
                return std::unexpected(status.error());
            }
        }
        return PinCheck{PinCheckStatus::Accepted, 0, std::chrono::seconds::zero()};
    }

    // A lockout that is over gives a fresh set of attempts.
    const int failed = credentials->lockedUntil ? 1 : credentials->failedAttempts + 1;
    if (failed >= kMaxFailedAttempts) {
        if (auto status = staff.setLoginState(id, failed, now + kLockoutDuration); !status) {
            return std::unexpected(status.error());
        }
        return PinCheck{PinCheckStatus::LockedOut, 0, kLockoutDuration};
    }
    if (auto status = staff.setLoginState(id, failed, std::nullopt); !status) {
        return std::unexpected(status.error());
    }
    return PinCheck{PinCheckStatus::WrongPin, kMaxFailedAttempts - failed,
                    std::chrono::seconds::zero()};
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
    const auto prepared = prepareAttempt(m_staff, m_clock, id, pin);
    if (!prepared) {
        return std::unexpected(prepared.error());
    }
    if (prepared->lockedFor.count() > 0) {
        return PinCheck{PinCheckStatus::LockedOut, 0, prepared->lockedFor};
    }

    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto check = recordAttempt(m_staff, m_clock, id, prepared->key);
    if (!check) {
        return check;
    }
    if (auto status = transaction->commit(); !status) {
        return std::unexpected(status.error());
    }
    if (check->status == PinCheckStatus::Accepted) {
        m_session.start(std::move(*user), prepared->mustChangePin);
    }
    return check;
}

void AuthService::logout() { m_session.clear(); }

core::Status AuthService::setRequiredPin(std::string_view newPin) {
    if (!m_session.user() || !m_session.mustChangePin()) {
        return std::unexpected(core::Error{
            core::ErrorCode::PermissionDenied, "the session user need not change the PIN", {}});
    }
    if (!isValidPin(newPin)) {
        return std::unexpected(malformedPin());
    }
    const core::StaffUserId id = m_session.user()->id;
    // Both hashes before the transaction, as everywhere.
    const auto credentials = m_staff.credentials(id);
    if (!credentials) {
        return std::unexpected(credentials.error());
    }
    if (equalInConstantTime(credentials->hash,
                            deriveKey(newPin, credentials->salt, credentials->iterations))) {
        return std::unexpected(core::Error{core::ErrorCode::InvalidInput,
                                           "the new PIN equals the one chosen by the Coach",
                                           "pin_same"});
    }
    const auto pin = hashPin(newPin);
    if (!pin) {
        return std::unexpected(pin.error());
    }
    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    if (auto status = m_staff.setPin(id, *pin, false); !status) {
        return status;
    }
    if (auto status = transaction->commit(); !status) {
        return status;
    }
    m_session.pinChanged();
    return {};
}

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
    const auto prepared = prepareAttempt(m_staff, m_clock, id, currentPin);
    if (!prepared) {
        return std::unexpected(prepared.error());
    }
    if (prepared->lockedFor.count() > 0) {
        return PinCheck{PinCheckStatus::LockedOut, 0, prepared->lockedFor};
    }
    // Before the transaction like the other hash, so also for a current PIN that turns out wrong.
    const auto pin = hashPin(newPin);
    if (!pin) {
        return std::unexpected(pin.error());
    }

    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto check = recordAttempt(m_staff, m_clock, id, prepared->key);
    if (!check) {
        return check;
    }
    if (check->status == PinCheckStatus::Accepted) {
        if (auto status = m_staff.setPin(id, *pin, false); !status) {
            return std::unexpected(status.error());
        }
    }
    if (auto status = transaction->commit(); !status) {
        return std::unexpected(status.error());
    }
    if (check->status == PinCheckStatus::Accepted) {
        m_session.pinChanged();
    }
    return check;
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
    const auto id = m_staff.create(draft, *hash, true);
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
    const bool ownPin = m_session.user() && m_session.user()->id == id;
    if (auto status = m_staff.setPin(id, *pin, !ownPin); !status) {
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
            m_session.rename(core::trimmed(draft.name));
        }
    }
    return {};
}

} // namespace fdss::services
