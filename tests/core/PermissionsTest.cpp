#include "core/domain/Permissions.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>

using namespace fdss::core;

namespace {

struct MatrixRow {
    Permission permission;
    bool coach;
    bool assistant;
    bool physio;
    bool analyst;
};

// The table of SPEC section 5, row by row.
constexpr std::array kMatrix{
    MatrixRow{Permission::ManageStaff, true, false, false, false},
    MatrixRow{Permission::ManageSettings, true, false, false, false},
    MatrixRow{Permission::EditSquad, true, true, false, false},
    MatrixRow{Permission::EditAvailability, true, true, true, false},
    MatrixRow{Permission::ViewAvailabilityDetails, true, false, true, false},
    MatrixRow{Permission::EditCalendar, true, true, false, false},
    MatrixRow{Permission::EditTraining, true, true, true, false},
    MatrixRow{Permission::EditLineup, true, true, false, false},
    MatrixRow{Permission::RunMatchDay, true, true, false, false},
    MatrixRow{Permission::Import, true, true, false, true},
    MatrixRow{Permission::ManageMetrics, true, true, false, true},
    MatrixRow{Permission::ViewAnalysis, true, true, true, true},
};

} // namespace

TEST(PermissionsTest, MatrixMatchesTheSpecRowByRow) {
    for (const MatrixRow& row : kMatrix) {
        SCOPED_TRACE(toString(row.permission));
        EXPECT_EQ(isAllowed(Role::Coach, row.permission), row.coach);
        EXPECT_EQ(isAllowed(Role::Assistant, row.permission), row.assistant);
        EXPECT_EQ(isAllowed(Role::Physio, row.permission), row.physio);
        EXPECT_EQ(isAllowed(Role::Analyst, row.permission), row.analyst);
    }
}

TEST(PermissionsTest, TheTableAboveCoversEveryPermission) {
    EXPECT_EQ(kMatrix.size(), allValues<Permission>().size());
    for (const Permission permission : allValues<Permission>()) {
        const auto covered = [&](const MatrixRow& row) { return row.permission == permission; };
        EXPECT_EQ(std::ranges::count_if(kMatrix, covered), 1) << toString(permission);
    }
}

TEST(PermissionsTest, NamesRoundTrip) {
    for (const Permission permission : allValues<Permission>()) {
        const auto parsed = permissionFromName(toString(permission));
        ASSERT_TRUE(parsed.has_value()) << toString(permission);
        EXPECT_EQ(*parsed, permission);
    }
}

TEST(PermissionsTest, UnknownNameFails) {
    for (const char* name : {"", "editsquad", "EditSquad ", "DropDatabase"}) {
        const auto parsed = permissionFromName(name);
        ASSERT_FALSE(parsed.has_value()) << name;
        EXPECT_EQ(parsed.error().code, ErrorCode::InvalidInput);
    }
}
