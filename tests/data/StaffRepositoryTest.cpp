#include "data/repositories/StaffRepository.hpp"

#include "RepositoryFixture.hpp"
#include "data/repositories/RepositorySupport.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <string>

using namespace fdss::core;
using namespace fdss::data;
using namespace fdss::tests;
using namespace std::chrono;

namespace {

class StaffRepositoryTest : public RepositoryTest {
protected:
    [[nodiscard]] StaffRepository repository() { return StaffRepository(*m_db); }

    static StaffUserDraft draft(const char* name = "Jan Trener", Role role = Role::Coach,
                                bool active = true) {
        StaffUserDraft result;
        result.name = name;
        result.role = role;
        result.active = active;
        return result;
    }

    static PinHash pin() {
        PinHash result;
        // Bytes that are not valid text and contain a zero: they must stay a blob.
        result.hash = {0x00, 0xFF, 0x10, 0x80};
        result.salt = {0xAB, 0x00, 0xCD};
        result.iterations = 120000;
        return result;
    }

    StaffUserId create(const char* name = "Jan Trener", Role role = Role::Coach,
                       bool active = true) {
        const auto id = repository().create(draft(name, role, active), pin());
        EXPECT_TRUE(id.has_value()) << id.error().detail;
        return id.value_or(StaffUserId{});
    }
};

constexpr StaffUserId kNoUser{999};

} // namespace

TEST_F(StaffRepositoryTest, CreatedUserCanBeRead) {
    const StaffUserId id = create("  Jan Trener ", Role::Physio);

    const auto user = repository().get(id);
    ASSERT_TRUE(user.has_value()) << user.error().detail;
    EXPECT_EQ(user->id, id);
    EXPECT_EQ(user->name, "Jan Trener");
    EXPECT_EQ(user->role, Role::Physio);
    EXPECT_TRUE(user->active);
}

TEST_F(StaffRepositoryTest, GetOfAMissingUserIsNotFound) {
    const auto user = repository().get(kNoUser);
    ASSERT_FALSE(user.has_value());
    EXPECT_EQ(user.error().code, ErrorCode::NotFound);
}

TEST_F(StaffRepositoryTest, EveryRoleIsStoredAndRead) {
    for (const Role role : allValues<Role>()) {
        const auto user = repository().get(create("Osoba", role));
        ASSERT_TRUE(user.has_value());
        EXPECT_EQ(user->role, role) << toDbText(role);
    }
}

TEST_F(StaffRepositoryTest, ListActiveSkipsDeactivatedUsersAndListAllDoesNot) {
    const StaffUserId active = create("Aktywny");
    const StaffUserId inactive = create("Nieaktywny", Role::Analyst, false);

    const auto activeUsers = repository().listActive();
    ASSERT_TRUE(activeUsers.has_value());
    ASSERT_EQ(activeUsers->size(), 1U);
    EXPECT_EQ(activeUsers->at(0).id, active);

    const auto allUsers = repository().listAll();
    ASSERT_TRUE(allUsers.has_value());
    ASSERT_EQ(allUsers->size(), 2U);
    EXPECT_EQ(allUsers->at(1).id, inactive);
    EXPECT_FALSE(allUsers->at(1).active);
}

TEST_F(StaffRepositoryTest, UpdateChangesNameRoleAndActiveButNotThePin) {
    const StaffUserId id = create();
    ASSERT_TRUE(
        repository().update(id, draft("  Nowa Nazwa ", Role::Assistant, false)).has_value());

    const auto user = repository().get(id);
    ASSERT_TRUE(user.has_value());
    EXPECT_EQ(user->name, "Nowa Nazwa");
    EXPECT_EQ(user->role, Role::Assistant);
    EXPECT_FALSE(user->active);

    const auto credentials = repository().credentials(id);
    ASSERT_TRUE(credentials.has_value());
    EXPECT_EQ(credentials->hash, pin().hash);
}

TEST_F(StaffRepositoryTest, UpdateOfAMissingUserIsNotFound) {
    const auto status = repository().update(kNoUser, draft());
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, ErrorCode::NotFound);
}

TEST_F(StaffRepositoryTest, EmptyNameIsInvalidInput) {
    const auto id = repository().create(draft("   "), pin());
    ASSERT_FALSE(id.has_value());
    EXPECT_EQ(id.error().code, ErrorCode::InvalidInput);
}

TEST_F(StaffRepositoryTest, IterationsMustBePositive) {
    PinHash noIterations = pin();
    noIterations.iterations = 0;
    const auto id = repository().create(draft(), noIterations);
    ASSERT_FALSE(id.has_value());
    EXPECT_EQ(id.error().code, ErrorCode::InvalidInput);
}

TEST_F(StaffRepositoryTest, CredentialsAreStoredAsBytes) {
    const StaffUserId id = create();

    const auto credentials = repository().credentials(id);
    ASSERT_TRUE(credentials.has_value()) << credentials.error().detail;
    EXPECT_EQ(credentials->hash, pin().hash);
    EXPECT_EQ(credentials->salt, pin().salt);
    EXPECT_EQ(credentials->iterations, 120000);
    EXPECT_EQ(credentials->failedAttempts, 0);
    EXPECT_EQ(credentials->lockedUntil, std::nullopt);

    EXPECT_EQ(scalar(m_db->database(),
                     QStringLiteral("SELECT typeof(pin_hash) FROM staff_user WHERE id = ?"),
                     {idValue(id)})
                  .toString(),
              QStringLiteral("blob"));
}

TEST_F(StaffRepositoryTest, CredentialsOfAMissingUserAreNotFound) {
    const auto credentials = repository().credentials(kNoUser);
    ASSERT_FALSE(credentials.has_value());
    EXPECT_EQ(credentials.error().code, ErrorCode::NotFound);
}

TEST_F(StaffRepositoryTest, SetPinReplacesHashSaltAndIterations) {
    const StaffUserId id = create();
    PinHash changed;
    changed.hash = {1, 2, 3};
    changed.salt = {4, 5};
    changed.iterations = 200000;
    ASSERT_TRUE(repository().setLoginState(id, 3, std::nullopt).has_value());
    ASSERT_TRUE(repository().setPin(id, changed).has_value());

    const auto credentials = repository().credentials(id);
    ASSERT_TRUE(credentials.has_value());
    EXPECT_EQ(credentials->hash, changed.hash);
    EXPECT_EQ(credentials->salt, changed.salt);
    EXPECT_EQ(credentials->iterations, 200000);
    EXPECT_EQ(credentials->failedAttempts, 3);
}

TEST_F(StaffRepositoryTest, SetPinOfAMissingUserIsNotFound) {
    const auto status = repository().setPin(kNoUser, pin());
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, ErrorCode::NotFound);
}

TEST_F(StaffRepositoryTest, SetLoginStateStoresTheLockoutInUtc) {
    const StaffUserId id = create();
    const Timestamp until = sys_days{2026y / November / 1d} + hours{10} + seconds{30};
    ASSERT_TRUE(repository().setLoginState(id, 5, until).has_value());

    const auto locked = repository().credentials(id);
    ASSERT_TRUE(locked.has_value()) << locked.error().detail;
    EXPECT_EQ(locked->failedAttempts, 5);
    EXPECT_EQ(locked->lockedUntil, until);
    EXPECT_EQ(locked->hash, pin().hash);
    EXPECT_EQ(scalar(m_db->database(),
                     QStringLiteral("SELECT locked_until FROM staff_user WHERE id = ?"),
                     {idValue(id)})
                  .toString(),
              QStringLiteral("2026-11-01T10:00:30Z"));

    ASSERT_TRUE(repository().setLoginState(id, 0, std::nullopt).has_value());
    const auto cleared = repository().credentials(id);
    ASSERT_TRUE(cleared.has_value());
    EXPECT_EQ(cleared->failedAttempts, 0);
    EXPECT_EQ(cleared->lockedUntil, std::nullopt);
}

// The schema allows these values; a plain conversion to int would read them as 0.
TEST_F(StaffRepositoryTest, CredentialsThatDoNotFitAnIntAreADatabaseError) {
    for (const char* column : {"iterations", "failed_attempts"}) {
        SCOPED_TRACE(column);
        const StaffUserId id = create();
        ASSERT_TRUE(run(m_db->database(),
                        QStringLiteral("UPDATE staff_user SET %1 = 4294967296 WHERE id = ?")
                            .arg(QString::fromLatin1(column)),
                        {idValue(id)}));

        const auto credentials = repository().credentials(id);
        ASSERT_FALSE(credentials.has_value());
        EXPECT_EQ(credentials.error().code, ErrorCode::Database);
        EXPECT_NE(credentials.error().detail.find(column), std::string::npos)
            << credentials.error().detail;
    }
}

TEST_F(StaffRepositoryTest, LockoutOutsideTheUtcFormatIsADatabaseError) {
    const StaffUserId id = create();
    for (const char* text : {"2026-11-01", "2026-11-01T10:00:30"}) {
        SCOPED_TRACE(text);
        ASSERT_TRUE(run(m_db->database(),
                        QStringLiteral("UPDATE staff_user SET locked_until = ? WHERE id = ?"),
                        {QString::fromLatin1(text), idValue(id)}));

        const auto credentials = repository().credentials(id);
        ASSERT_FALSE(credentials.has_value());
        EXPECT_EQ(credentials.error().code, ErrorCode::Database);
    }
}

TEST_F(StaffRepositoryTest, SetLoginStateOfAMissingUserIsNotFound) {
    const auto status = repository().setLoginState(kNoUser, 1, std::nullopt);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, ErrorCode::NotFound);
}

TEST_F(StaffRepositoryTest, CountActiveCoachesSkipsOtherRolesAndDeactivatedUsers) {
    EXPECT_EQ(repository().countActiveCoaches().value(), 0);
    create("Trener 1");
    const StaffUserId second = create("Trener 2");
    create("Trener nieaktywny", Role::Coach, false);
    create("Asystent", Role::Assistant);
    EXPECT_EQ(repository().countActiveCoaches().value(), 2);

    ASSERT_TRUE(repository().update(second, draft("Trener 2", Role::Coach, false)).has_value());
    EXPECT_EQ(repository().countActiveCoaches().value(), 1);
}
