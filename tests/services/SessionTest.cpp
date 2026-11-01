#include "services/Session.hpp"

#include <gtest/gtest.h>

#include <string>

using namespace fdss::core;
using fdss::services::Session;

namespace {

StaffUser user(Role role) {
    StaffUser result;
    result.id = StaffUserId{7};
    result.name = "Jan";
    result.role = role;
    return result;
}

} // namespace

TEST(SessionTest, NobodyLoggedInAllowsNothing) {
    const Session session;
    EXPECT_FALSE(session.user().has_value());
    for (const Permission permission : allValues<Permission>()) {
        EXPECT_FALSE(session.isAllowed(permission)) << toString(permission);
        const Status status = session.require(permission);
        ASSERT_FALSE(status.has_value());
        EXPECT_EQ(status.error().code, ErrorCode::PermissionDenied);
    }
}

TEST(SessionTest, FollowsTheCoreMatrixForEveryRole) {
    for (const Role role : allValues<Role>()) {
        Session session;
        session.start(user(role));
        for (const Permission permission : allValues<Permission>()) {
            EXPECT_EQ(session.isAllowed(permission), isAllowed(role, permission))
                << toDbText(role) << " " << toString(permission);
            EXPECT_EQ(session.require(permission).has_value(), isAllowed(role, permission));
        }
    }
}

TEST(SessionTest, ClearEndsTheSession) {
    Session session;
    session.start(user(Role::Coach));
    ASSERT_TRUE(session.isAllowed(Permission::ManageStaff));

    session.clear();

    EXPECT_FALSE(session.user().has_value());
    EXPECT_FALSE(session.isAllowed(Permission::ManageStaff));
}
