#include "services/AuthService.hpp"

#include "ServiceFixture.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace fdss::core;
using namespace fdss::services;
using namespace fdss::tests;
using namespace std::chrono_literals;

namespace {

class AuthServiceTest : public ServiceTest {
protected:
    // Five wrong PINs: the user is locked for kLockoutDuration from now on.
    void lock(StaffUserId id) {
        for (int attempt = 0; attempt < kMaxFailedAttempts; ++attempt) {
            const auto check = m_auth->login(id, "9999");
            ASSERT_TRUE(check.has_value()) << check.error().detail;
        }
        ASSERT_EQ(m_auth->lockRemaining(id).value_or(0s), kLockoutDuration);
    }
};

constexpr StaffUserId kNoUser{999};

} // namespace

TEST_F(AuthServiceTest, PinPolicyIsFourToEightAsciiDigits) {
    EXPECT_TRUE(AuthService::isValidPin("1234"));
    EXPECT_TRUE(AuthService::isValidPin("12345678"));
    EXPECT_TRUE(AuthService::isValidPin("0000"));

    EXPECT_FALSE(AuthService::isValidPin(""));
    EXPECT_FALSE(AuthService::isValidPin("123"));
    EXPECT_FALSE(AuthService::isValidPin("123456789"));
    EXPECT_FALSE(AuthService::isValidPin("12a4"));
    EXPECT_FALSE(AuthService::isValidPin("12 4"));
    EXPECT_FALSE(AuthService::isValidPin(" 1234"));
    EXPECT_FALSE(AuthService::isValidPin("١٢٣٤")); // digits, but not ASCII ones
}

TEST_F(AuthServiceTest, HashHasTheParametersOfAdr007) {
    const auto hash = m_auth->hashPin(kPin);
    ASSERT_TRUE(hash.has_value()) << hash.error().detail;
    EXPECT_EQ(hash->hash.size(), 32U);
    EXPECT_EQ(hash->salt.size(), 16U);
    EXPECT_EQ(hash->iterations, kIterations);
}

TEST_F(AuthServiceTest, SamePinGivesDifferentHash) {
    const auto first = m_auth->hashPin(kPin);
    const auto second = m_auth->hashPin(kPin);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(first->salt, second->salt);
    EXPECT_NE(first->hash, second->hash);
}

TEST_F(AuthServiceTest, HashingAMalformedPinIsInvalidInput) {
    expectError(m_auth->hashPin("12"), ErrorCode::InvalidInput, "pin");
}

TEST_F(AuthServiceTest, RightPinLogsIn) {
    const StaffUserId id = addUser("Jan", Role::Physio);

    const auto check = m_auth->login(id, kPin);

    ASSERT_TRUE(check.has_value()) << check.error().detail;
    EXPECT_EQ(check->status, PinCheckStatus::Accepted);
    ASSERT_TRUE(m_session.user().has_value());
    EXPECT_EQ(m_session.user()->id, id);
    EXPECT_EQ(m_session.user()->name, "Jan");
    EXPECT_EQ(m_session.user()->role, Role::Physio);
}

TEST_F(AuthServiceTest, WrongPinIsRefusedAndSaysWhatIsLeft) {
    const StaffUserId id = addUser("Jan", Role::Coach);

    const auto check = m_auth->login(id, "4321");

    ASSERT_TRUE(check.has_value()) << check.error().detail;
    EXPECT_EQ(check->status, PinCheckStatus::WrongPin);
    EXPECT_EQ(check->remainingAttempts, kMaxFailedAttempts - 1);
    EXPECT_FALSE(m_session.user().has_value());
    EXPECT_EQ(credentials(id).failedAttempts, 1);
}

// Expected key computed independently: Python hashlib.pbkdf2_hmac and openssl kdf agree on it.
TEST_F(AuthServiceTest, ChecksThePbkdf2KeyOfAnotherTool) {
    PinHash pin;
    pin.salt = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    pin.iterations = 1000;
    pin.hash = {0x22, 0x40, 0xea, 0x2a, 0x22, 0x75, 0x4a, 0x3c, 0x46, 0x10, 0x35,
                0x6e, 0xf8, 0xe4, 0xd7, 0xac, 0xf2, 0x5a, 0xf2, 0xac, 0x3d, 0x2d,
                0x11, 0x86, 0x6d, 0xe9, 0x7a, 0x8b, 0x78, 0x12, 0xc5, 0x2c};
    const auto id = m_staff->create(draft("Jan", Role::Coach), pin);
    ASSERT_TRUE(id.has_value()) << id.error().detail;

    const auto wrong = m_auth->login(*id, "1235");
    ASSERT_TRUE(wrong.has_value());
    EXPECT_EQ(wrong->status, PinCheckStatus::WrongPin);

    const auto right = m_auth->login(*id, "1234");
    ASSERT_TRUE(right.has_value());
    EXPECT_EQ(right->status, PinCheckStatus::Accepted);
}

TEST_F(AuthServiceTest, UsesTheStoredIterationsNotItsOwn) {
    const StaffUserId id = addUser("Jan", Role::Coach);

    AuthService stronger(*m_db, *m_staff, m_session, m_clock, kIterations * 2);
    const auto check = stronger.login(id, kPin);

    ASSERT_TRUE(check.has_value()) << check.error().detail;
    EXPECT_EQ(check->status, PinCheckStatus::Accepted);
    EXPECT_EQ(credentials(id).iterations, kIterations);
}

TEST_F(AuthServiceTest, AnEmptyStoredHashMatchesNothing) {
    const StaffUserId id = addUser("Jan", Role::Coach);
    // Raw SQL: through the repository an empty vector binds NULL, which the schema refuses.
    ASSERT_TRUE(sql("UPDATE staff_user SET pin_hash = x'' WHERE id = ?",
                    {QVariant(static_cast<qlonglong>(id.value))}));

    const auto check = m_auth->login(id, kPin);

    ASSERT_TRUE(check.has_value()) << check.error().detail;
    EXPECT_EQ(check->status, PinCheckStatus::WrongPin);
    EXPECT_FALSE(m_session.user().has_value());
}

TEST_F(AuthServiceTest, MalformedPinIsNotAnAttempt) {
    const StaffUserId id = addUser("Jan", Role::Coach);

    expectError(m_auth->login(id, "12"), ErrorCode::InvalidInput, "pin");

    EXPECT_EQ(credentials(id).failedAttempts, 0);
}

TEST_F(AuthServiceTest, DeactivatedUserCannotLogIn) {
    const StaffUserId id = addUser("Jan", Role::Coach, false);

    expectError(m_auth->login(id, kPin), ErrorCode::PermissionDenied);

    EXPECT_FALSE(m_session.user().has_value());
}

TEST_F(AuthServiceTest, UnknownUserIsNotFound) {
    expectError(m_auth->login(kNoUser, kPin), ErrorCode::NotFound);
    expectError(m_auth->lockRemaining(kNoUser), ErrorCode::NotFound);
}

TEST_F(AuthServiceTest, LogoutEndsTheSession) {
    logIn(addUser("Jan", Role::Coach));

    m_auth->logout();

    EXPECT_FALSE(m_session.user().has_value());
}

TEST_F(AuthServiceTest, ListActiveStaffNeedsNoSession) {
    addUser("Jan", Role::Coach);
    addUser("Ewa", Role::Physio, false);

    const auto staff = m_auth->listActiveStaff();

    ASSERT_TRUE(staff.has_value()) << staff.error().detail;
    ASSERT_EQ(staff->size(), 1U);
    EXPECT_EQ(staff->front().name, "Jan");
}

TEST_F(AuthServiceTest, FifthWrongPinLocksTheUser) {
    const StaffUserId id = addUser("Jan", Role::Coach);

    for (int attempt = 1; attempt < kMaxFailedAttempts; ++attempt) {
        const auto check = m_auth->login(id, "9999");
        ASSERT_TRUE(check.has_value());
        EXPECT_EQ(check->status, PinCheckStatus::WrongPin);
        EXPECT_EQ(check->remainingAttempts, kMaxFailedAttempts - attempt);
    }

    const auto fifth = m_auth->login(id, "9999");

    ASSERT_TRUE(fifth.has_value());
    EXPECT_EQ(fifth->status, PinCheckStatus::LockedOut);
    EXPECT_EQ(fifth->lockedFor, kLockoutDuration);
    EXPECT_EQ(credentials(id).failedAttempts, kMaxFailedAttempts);
}

TEST_F(AuthServiceTest, LockoutEndIsStoredAsUtcIsoText) {
    const StaffUserId id = addUser("Jan", Role::Coach);

    lock(id);

    EXPECT_EQ(scalar(m_db->database(),
                     QStringLiteral("SELECT locked_until FROM staff_user WHERE id = ?"),
                     {QVariant(static_cast<qlonglong>(id.value))})
                  .toString(),
              QStringLiteral("2026-11-01T12:00:30Z"));
}

TEST_F(AuthServiceTest, EvenTheRightPinIsRefusedDuringALockout) {
    const StaffUserId id = addUser("Jan", Role::Coach);
    lock(id);
    const StaffCredentials atLock = credentials(id);
    m_clock.advance(10s);

    const auto check = m_auth->login(id, kPin);

    ASSERT_TRUE(check.has_value());
    EXPECT_EQ(check->status, PinCheckStatus::LockedOut);
    EXPECT_EQ(check->lockedFor, 20s);
    EXPECT_FALSE(m_session.user().has_value());
    // An attempt does not extend the lockout.
    EXPECT_EQ(credentials(id).lockedUntil, atLock.lockedUntil);
    EXPECT_EQ(credentials(id).failedAttempts, atLock.failedAttempts);
}

TEST_F(AuthServiceTest, LockoutEndsExactlyWhenItSays) {
    const StaffUserId id = addUser("Jan", Role::Coach);
    lock(id);

    m_clock.advance(kLockoutDuration - 1s);
    EXPECT_EQ(m_auth->lockRemaining(id).value_or(0s), 1s);

    m_clock.advance(1s);
    EXPECT_EQ(m_auth->lockRemaining(id).value_or(1s), 0s);
    logIn(id);

    EXPECT_EQ(credentials(id).failedAttempts, 0);
    EXPECT_FALSE(credentials(id).lockedUntil.has_value());
}

TEST_F(AuthServiceTest, ExpiredLockoutGivesAFreshSetOfAttempts) {
    const StaffUserId id = addUser("Jan", Role::Coach);
    lock(id);
    m_clock.advance(kLockoutDuration);

    const auto check = m_auth->login(id, "9999");

    ASSERT_TRUE(check.has_value());
    EXPECT_EQ(check->status, PinCheckStatus::WrongPin);
    EXPECT_EQ(check->remainingAttempts, kMaxFailedAttempts - 1);
    EXPECT_EQ(credentials(id).failedAttempts, 1);
    EXPECT_FALSE(credentials(id).lockedUntil.has_value());
}

TEST_F(AuthServiceTest, RightPinClearsTheCounter) {
    const StaffUserId id = addUser("Jan", Role::Coach);
    ASSERT_TRUE(m_auth->login(id, "9999").has_value());
    ASSERT_TRUE(m_auth->login(id, "9999").has_value());

    logIn(id);

    EXPECT_EQ(credentials(id).failedAttempts, 0);
}

TEST_F(AuthServiceTest, LockoutSurvivesARestart) {
    const StaffUserId id = addUser("Jan", Role::Coach);
    lock(id);

    Session freshSession;
    const AuthService restarted(*m_db, *m_staff, freshSession, m_clock, kIterations);

    EXPECT_EQ(restarted.lockRemaining(id).value_or(0s), kLockoutDuration);
}

TEST_F(AuthServiceTest, ClockMovedBackDoesNotExtendALockout) {
    const StaffUserId id = addUser("Jan", Role::Coach);
    lock(id);

    m_clock.advance(-24h);

    EXPECT_EQ(m_auth->lockRemaining(id).value_or(1s), 0s);
    logIn(id);
}

TEST_F(AuthServiceTest, ChangeOwnPinNeedsALoggedInUser) {
    expectError(m_auth->changeOwnPin(kPin, "5678"), ErrorCode::PermissionDenied);
}

TEST_F(AuthServiceTest, ChangeOwnPinReplacesThePin) {
    const StaffUserId id = addUser("Jan", Role::Analyst);
    logIn(id);

    const auto check = m_auth->changeOwnPin(kPin, "5678");

    ASSERT_TRUE(check.has_value()) << check.error().detail;
    EXPECT_EQ(check->status, PinCheckStatus::Accepted);
    logIn(id, "5678");
    const auto old = m_auth->login(id, kPin);
    ASSERT_TRUE(old.has_value());
    EXPECT_EQ(old->status, PinCheckStatus::WrongPin);
}

TEST_F(AuthServiceTest, ChangeOwnPinRefusesAMalformedNewPinWithoutCountingIt) {
    const StaffUserId id = addUser("Jan", Role::Coach);
    logIn(id);

    expectError(m_auth->changeOwnPin(kPin, "12"), ErrorCode::InvalidInput, "pin");

    EXPECT_EQ(credentials(id).failedAttempts, 0);
    logIn(id);
}

TEST_F(AuthServiceTest, ChangeOwnPinCountsAWrongCurrentPinTowardsTheLockout) {
    const StaffUserId id = addUser("Jan", Role::Coach);
    logIn(id);

    for (int attempt = 1; attempt < kMaxFailedAttempts; ++attempt) {
        const auto check = m_auth->changeOwnPin("9999", "5678");
        ASSERT_TRUE(check.has_value());
        EXPECT_EQ(check->status, PinCheckStatus::WrongPin);
    }
    const auto fifth = m_auth->changeOwnPin("9999", "5678");
    ASSERT_TRUE(fifth.has_value());
    EXPECT_EQ(fifth->status, PinCheckStatus::LockedOut);

    const auto refused = m_auth->changeOwnPin(kPin, "5678");
    ASSERT_TRUE(refused.has_value());
    EXPECT_EQ(refused->status, PinCheckStatus::LockedOut);

    // Nothing was changed: the old PIN opens the account once the lockout is over.
    m_clock.advance(kLockoutDuration);
    logIn(id);
}

TEST_F(AuthServiceTest, ChangeOwnPinIsOneTransaction) {
    const StaffUserId id = addUser("Jan", Role::Coach);
    logIn(id);
    ASSERT_TRUE(m_auth->changeOwnPin("9999", "5678").has_value());
    ASSERT_EQ(credentials(id).failedAttempts, 1);
    ASSERT_TRUE(sql("CREATE TRIGGER forced_failure BEFORE UPDATE OF pin_hash ON staff_user "
                    "BEGIN SELECT RAISE(ABORT, 'forced failure'); END"));

    EXPECT_FALSE(m_auth->changeOwnPin(kPin, "5678").has_value());

    // The counter would have been cleared by the same attempt.
    EXPECT_EQ(credentials(id).failedAttempts, 1);
    ASSERT_TRUE(sql("DROP TRIGGER forced_failure"));
    logIn(id);
}

TEST_F(AuthServiceTest, FailedStoreOfTheAttemptDoesNotLogIn) {
    const StaffUserId id = addUser("Jan", Role::Coach);
    ASSERT_TRUE(m_auth->login(id, "9999").has_value());
    ASSERT_TRUE(sql("CREATE TRIGGER forced_failure BEFORE UPDATE OF failed_attempts ON staff_user "
                    "BEGIN SELECT RAISE(ABORT, 'forced failure'); END"));

    EXPECT_FALSE(m_auth->login(id, kPin).has_value());
    EXPECT_FALSE(m_session.user().has_value());

    // A wrong PIN that cannot be counted is an error too, not a free attempt.
    EXPECT_FALSE(m_auth->login(id, "9999").has_value());
}

// --- Staff management as Coach ---

class StaffManagementTest : public AuthServiceTest {
protected:
    void SetUp() override {
        AuthServiceTest::SetUp();
        if (HasFatalFailure()) {
            return;
        }
        m_coach = addUser("Jan Trener", Role::Coach);
        logIn(m_coach);
    }

    StaffUserId m_coach;
};

TEST_F(StaffManagementTest, AddStaffStoresAUserWhoCanLogIn) {
    const auto id = m_auth->addStaff(draft("  Ewa  ", Role::Physio), "5678");

    ASSERT_TRUE(id.has_value()) << id.error().detail;
    EXPECT_EQ(stored(*id).name, "Ewa");
    EXPECT_EQ(stored(*id).role, Role::Physio);
    EXPECT_EQ(credentials(*id).iterations, kIterations);
    logIn(*id, "5678");
}

TEST_F(StaffManagementTest, AddStaffValidatesTheDraftAndThePin) {
    expectError(m_auth->addStaff(draft("   ", Role::Physio), "5678"), ErrorCode::InvalidInput,
                "name");
    expectError(m_auth->addStaff(draft("Ewa", Role::Physio), "12"), ErrorCode::InvalidInput, "pin");

    EXPECT_EQ(count("SELECT count(*) FROM staff_user"), 1);
}

TEST_F(StaffManagementTest, ListAllStaffIncludesDeactivatedUsers) {
    addUser("Ewa", Role::Physio, false);

    const auto staff = m_auth->listAllStaff();

    ASSERT_TRUE(staff.has_value()) << staff.error().detail;
    EXPECT_EQ(staff->size(), 2U);
}

TEST_F(StaffManagementTest, UpdateStaffChangesNameAndRole) {
    const StaffUserId id = addUser("Ewa", Role::Physio);

    ASSERT_TRUE(m_auth->updateStaff(id, draft("Ewa Nowak", Role::Analyst)).has_value());

    EXPECT_EQ(stored(id).name, "Ewa Nowak");
    EXPECT_EQ(stored(id).role, Role::Analyst);
}

TEST_F(StaffManagementTest, UpdateStaffValidatesTheDraft) {
    const StaffUserId id = addUser("Ewa", Role::Physio);

    expectError(m_auth->updateStaff(id, draft("", Role::Physio)), ErrorCode::InvalidInput, "name");

    EXPECT_EQ(stored(id).name, "Ewa");
}

TEST_F(StaffManagementTest, ChangingAnUnknownUserIsNotFound) {
    expectError(m_auth->updateStaff(kNoUser, draft("Ewa", Role::Physio)), ErrorCode::NotFound);
    expectError(m_auth->deactivate(kNoUser), ErrorCode::NotFound);
    expectError(m_auth->reactivate(kNoUser), ErrorCode::NotFound);
    expectError(m_auth->resetPin(kNoUser, "5678"), ErrorCode::NotFound);
}

TEST_F(StaffManagementTest, DeactivateAndReactivateFlipActive) {
    const StaffUserId id = addUser("Ewa", Role::Physio);

    ASSERT_TRUE(m_auth->deactivate(id).has_value());
    EXPECT_FALSE(stored(id).active);
    expectError(m_auth->login(id, kPin), ErrorCode::PermissionDenied);

    ASSERT_TRUE(m_auth->reactivate(id).has_value());
    EXPECT_TRUE(stored(id).active);
    EXPECT_EQ(stored(id).name, "Ewa");
    EXPECT_EQ(stored(id).role, Role::Physio);
}

TEST_F(StaffManagementTest, ResetPinReplacesThePinAndLiftsTheLockout) {
    const StaffUserId id = addUser("Ewa", Role::Physio);
    lock(id);
    logIn(m_coach);

    ASSERT_TRUE(m_auth->resetPin(id, "5678").has_value());

    EXPECT_EQ(credentials(id).failedAttempts, 0);
    EXPECT_FALSE(credentials(id).lockedUntil.has_value());
    logIn(id, "5678");
}

TEST_F(StaffManagementTest, ResetPinRefusesAMalformedPin) {
    const StaffUserId id = addUser("Ewa", Role::Physio);
    const PinHash before = credentials(id);

    expectError(m_auth->resetPin(id, "12"), ErrorCode::InvalidInput, "pin");

    EXPECT_EQ(credentials(id).hash, before.hash);
}

// --- Last active Coach ---

TEST_F(StaffManagementTest, LastCoachCannotBeDeactivated) {
    expectError(m_auth->deactivate(m_coach), ErrorCode::Conflict, "last_coach");

    EXPECT_TRUE(stored(m_coach).active);
    EXPECT_TRUE(m_session.user().has_value());
}

TEST_F(StaffManagementTest, LastCoachCannotBeDemoted) {
    expectError(m_auth->updateStaff(m_coach, draft("Jan Trener", Role::Assistant)),
                ErrorCode::Conflict, "last_coach");

    EXPECT_EQ(stored(m_coach).role, Role::Coach);
}

TEST_F(StaffManagementTest, LastCoachCannotBeDeactivatedThroughUpdate) {
    expectError(m_auth->updateStaff(m_coach, draft("Jan Trener", Role::Coach, false)),
                ErrorCode::Conflict, "last_coach");

    EXPECT_TRUE(stored(m_coach).active);
}

TEST_F(StaffManagementTest, DeactivatedCoachDoesNotCountAsAnother) {
    addUser("Stary Trener", Role::Coach, false);

    expectError(m_auth->deactivate(m_coach), ErrorCode::Conflict, "last_coach");
}

TEST_F(StaffManagementTest, LastCoachCanBeRenamed) {
    ASSERT_TRUE(m_auth->updateStaff(m_coach, draft("Jan Nowy", Role::Coach)).has_value());

    EXPECT_EQ(stored(m_coach).name, "Jan Nowy");
}

TEST_F(StaffManagementTest, ACoachCanGoWhenAnotherStays) {
    const StaffUserId other = addUser("Ewa", Role::Coach);

    ASSERT_TRUE(m_auth->updateStaff(other, draft("Ewa", Role::Assistant)).has_value());
    EXPECT_EQ(stored(other).role, Role::Assistant);

    expectError(m_auth->deactivate(m_coach), ErrorCode::Conflict, "last_coach");
}

// --- Changing the session user ---

TEST_F(StaffManagementTest, RenamingTheSessionUserUpdatesTheSession) {
    const int serial = m_session.serial();

    ASSERT_TRUE(m_auth->updateStaff(m_coach, draft("  Jan Nowy ", Role::Coach)).has_value());

    ASSERT_TRUE(m_session.user().has_value());
    EXPECT_EQ(m_session.user()->name, "Jan Nowy");
    EXPECT_EQ(m_session.serial(), serial);
}

TEST_F(StaffManagementTest, DemotingTheSessionUserEndsTheSession) {
    addUser("Ewa", Role::Coach);

    ASSERT_TRUE(m_auth->updateStaff(m_coach, draft("Jan Trener", Role::Assistant)).has_value());

    EXPECT_FALSE(m_session.user().has_value());
}

TEST_F(StaffManagementTest, DeactivatingTheSessionUserEndsTheSession) {
    addUser("Ewa", Role::Coach);

    ASSERT_TRUE(m_auth->deactivate(m_coach).has_value());

    EXPECT_FALSE(m_session.user().has_value());
}

TEST_F(StaffManagementTest, ChangingSomeoneElseKeepsTheSession) {
    const StaffUserId other = addUser("Ewa", Role::Physio);

    ASSERT_TRUE(m_auth->updateStaff(other, draft("Ewa", Role::Analyst)).has_value());
    ASSERT_TRUE(m_auth->deactivate(other).has_value());

    ASSERT_TRUE(m_session.user().has_value());
    EXPECT_EQ(m_session.user()->id, m_coach);
}

// --- Permission denial: every staff method, every role without ManageStaff, and logged out ---

class StaffPermissionTest : public ServiceTest,
                            public ::testing::WithParamInterface<std::optional<Role>> {
protected:
    void SetUp() override {
        ServiceTest::SetUp();
        if (HasFatalFailure()) {
            return;
        }
        m_target = addUser("Cel", Role::Physio);
        if (GetParam()) {
            logIn(addUser("Sesja", *GetParam()));
        }
    }

    // What the refused call must not have touched.
    void expectTargetUntouched() const {
        EXPECT_EQ(stored(m_target).name, "Cel");
        EXPECT_EQ(stored(m_target).role, Role::Physio);
        EXPECT_TRUE(stored(m_target).active);
    }

    StaffUserId m_target;
};

TEST_P(StaffPermissionTest, ListAllStaffIsDenied) {
    expectError(m_auth->listAllStaff(), ErrorCode::PermissionDenied);
}

TEST_P(StaffPermissionTest, AddStaffIsDenied) {
    const int before = count("SELECT count(*) FROM staff_user");

    expectError(m_auth->addStaff(draft("Nowy", Role::Analyst), kPin), ErrorCode::PermissionDenied);

    EXPECT_EQ(count("SELECT count(*) FROM staff_user"), before);
}

TEST_P(StaffPermissionTest, UpdateStaffIsDenied) {
    expectError(m_auth->updateStaff(m_target, draft("Zmiana", Role::Coach)),
                ErrorCode::PermissionDenied);

    expectTargetUntouched();
}

TEST_P(StaffPermissionTest, ResetPinIsDenied) {
    const PinHash before = credentials(m_target);

    expectError(m_auth->resetPin(m_target, "5678"), ErrorCode::PermissionDenied);

    EXPECT_EQ(credentials(m_target).hash, before.hash);
}

TEST_P(StaffPermissionTest, DeactivateIsDenied) {
    expectError(m_auth->deactivate(m_target), ErrorCode::PermissionDenied);

    expectTargetUntouched();
}

TEST_P(StaffPermissionTest, ReactivateIsDenied) {
    const StaffUserId inactive = addUser("Stary", Role::Analyst, false);

    expectError(m_auth->reactivate(inactive), ErrorCode::PermissionDenied);

    EXPECT_FALSE(stored(inactive).active);
}

INSTANTIATE_TEST_SUITE_P(
    RolesWithoutManageStaff, StaffPermissionTest,
    ::testing::Values(std::optional<Role>(Role::Assistant), std::optional<Role>(Role::Physio),
                      std::optional<Role>(Role::Analyst), std::optional<Role>()),
    [](const ::testing::TestParamInfo<std::optional<Role>>& info) {
        return info.param ? std::string(toDbText(*info.param)) : std::string("loggedOut");
    });

// --- Two connections to one file, as with a second FootballDSS process ---

namespace {

// Lets a test run a step of the other connection at the next read of the clock. An attempt reads
// the clock right after its first look at the credentials, before the hash and the transaction:
// the window in which an earlier read of the counter would go stale.
class InterleavingClock final : public Clock {
public:
    [[nodiscard]] Timestamp now() const override {
        if (m_beforeNextRead) {
            const std::function<void()> step = std::exchange(m_beforeNextRead, nullptr);
            step();
        }
        return m_clock.now();
    }
    [[nodiscard]] Date today() const override { return m_clock.today(); }

    void beforeNextRead(std::function<void()> step) { m_beforeNextRead = std::move(step); }

private:
    FakeClock m_clock;
    mutable std::function<void()> m_beforeNextRead;
};

struct Connection {
    explicit Connection(std::unique_ptr<fdss::data::DatabaseManager> opened)
        : db(std::move(opened)), staff(*db), auth(*db, staff, session, clock, 4) {}

    std::unique_ptr<fdss::data::DatabaseManager> db;
    fdss::data::StaffRepository staff;
    InterleavingClock clock;
    Session session;
    AuthService auth;
};

class TwoConnectionsTest : public TempDatabaseTest {
protected:
    static constexpr const char* kRightPin = "1234";
    static constexpr const char* kWrongPin = "9999";

    void SetUp() override {
        TempDatabaseTest::SetUp();
        m_first = open();
        m_second = open();
        ASSERT_TRUE(m_first && m_second);

        const auto hash = m_first->auth.hashPin(kRightPin);
        ASSERT_TRUE(hash.has_value());
        StaffUserDraft coach;
        coach.name = "Jan";
        coach.role = Role::Coach;
        const auto id = m_first->staff.create(coach, *hash);
        ASSERT_TRUE(id.has_value()) << id.error().detail;
        m_user = *id;
    }

    [[nodiscard]] std::unique_ptr<Connection> open() const {
        auto opened = fdss::data::DatabaseManager::open(databasePath(), options());
        EXPECT_TRUE(opened.has_value()) << opened.error().detail;
        return opened ? std::make_unique<Connection>(std::move(*opened)) : nullptr;
    }

    // One complete wrong attempt on the second connection, while the first is in the middle of its
    // own.
    void secondConnectionFailsOnceDuringTheNextAttempt() {
        m_first->clock.beforeNextRead([this] {
            const auto check = m_second->auth.login(m_user, kWrongPin);
            ASSERT_TRUE(check.has_value()) << check.error().detail;
        });
    }

    [[nodiscard]] int storedFailures() const {
        return scalar(m_first->db->database(),
                      QStringLiteral("SELECT failed_attempts FROM staff_user WHERE id = ?"),
                      {QVariant(static_cast<qlonglong>(m_user.value))})
            .toInt();
    }

    std::unique_ptr<Connection> m_first;
    std::unique_ptr<Connection> m_second;
    StaffUserId m_user;
};

} // namespace

TEST_F(TwoConnectionsTest, WrongPinsOfBothConnectionsAddUp) {
    secondConnectionFailsOnceDuringTheNextAttempt();

    const auto check = m_first->auth.login(m_user, kWrongPin);

    ASSERT_TRUE(check.has_value()) << check.error().detail;
    EXPECT_EQ(check->status, PinCheckStatus::WrongPin);
    EXPECT_EQ(check->remainingAttempts, kMaxFailedAttempts - 2);
    EXPECT_EQ(storedFailures(), 2);
}

TEST_F(TwoConnectionsTest, AWrongCurrentPinAddsUpWithTheOtherConnection) {
    const auto login = m_first->auth.login(m_user, kRightPin);
    ASSERT_TRUE(login.has_value()) << login.error().detail;
    ASSERT_EQ(login->status, PinCheckStatus::Accepted);
    secondConnectionFailsOnceDuringTheNextAttempt();

    const auto check = m_first->auth.changeOwnPin(kWrongPin, "5678");

    ASSERT_TRUE(check.has_value()) << check.error().detail;
    EXPECT_EQ(check->status, PinCheckStatus::WrongPin);
    EXPECT_EQ(check->remainingAttempts, kMaxFailedAttempts - 2);
    EXPECT_EQ(storedFailures(), 2);
}

TEST_F(TwoConnectionsTest, ALockoutFromTheOtherConnectionStopsAnAttemptUnderWay) {
    for (int attempt = 1; attempt < kMaxFailedAttempts; ++attempt) {
        ASSERT_TRUE(m_first->auth.login(m_user, kWrongPin).has_value());
    }
    secondConnectionFailsOnceDuringTheNextAttempt(); // the fifth one

    const auto check = m_first->auth.login(m_user, kRightPin);

    ASSERT_TRUE(check.has_value()) << check.error().detail;
    EXPECT_EQ(check->status, PinCheckStatus::LockedOut);
    EXPECT_EQ(check->lockedFor, kLockoutDuration);
    EXPECT_FALSE(m_first->session.user().has_value());
    EXPECT_EQ(storedFailures(), kMaxFailedAttempts);
}
