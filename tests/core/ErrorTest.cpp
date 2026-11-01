#include "core/util/Error.hpp"

#include <gtest/gtest.h>

#include <array>
#include <set>
#include <string_view>

using fdss::core::Error;
using fdss::core::ErrorCode;
using fdss::core::Result;
using fdss::core::Status;

namespace {

constexpr std::array kAllCodes{ErrorCode::NotFound,     ErrorCode::InvalidInput,
                               ErrorCode::Conflict,     ErrorCode::PermissionDenied,
                               ErrorCode::Database,     ErrorCode::DatabaseLocked,
                               ErrorCode::NotADatabase, ErrorCode::IncompatibleVersion,
                               ErrorCode::Io,           ErrorCode::UnsupportedFormat,
                               ErrorCode::Internal};

Result<int> half(int value) {
    if (value % 2 != 0) {
        return std::unexpected(Error{ErrorCode::InvalidInput, "odd value", "value"});
    }
    return value / 2;
}

} // namespace

TEST(ErrorTest, EveryCodeHasItsOwnName) {
    std::set<std::string_view> names;
    for (const ErrorCode code : kAllCodes) {
        const std::string_view name = fdss::core::toString(code);
        EXPECT_FALSE(name.empty());
        EXPECT_NE(name, "Unknown");
        names.insert(name);
    }
    EXPECT_EQ(names.size(), kAllCodes.size());
}

TEST(ErrorTest, NamesMatchTheEnumerators) {
    EXPECT_EQ(fdss::core::toString(ErrorCode::NotADatabase), "NotADatabase");
    EXPECT_EQ(fdss::core::toString(ErrorCode::Io), "Io");
}

TEST(ErrorTest, DefaultErrorIsInternalWithoutText) {
    const Error error;
    EXPECT_EQ(error.code, ErrorCode::Internal);
    EXPECT_TRUE(error.detail.empty());
    EXPECT_TRUE(error.field.empty());
}

TEST(ErrorTest, ResultCarriesValueOrError) {
    const Result<int> ok = half(4);
    ASSERT_TRUE(ok.has_value());
    EXPECT_EQ(*ok, 2);

    const Result<int> failed = half(3);
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(failed.error().detail, "odd value");
    EXPECT_EQ(failed.error().field, "value");
}

TEST(ErrorTest, StatusCarriesSuccessOrError) {
    const Status ok;
    EXPECT_TRUE(ok.has_value());

    const Status failed = std::unexpected(Error{ErrorCode::Io, "disk full", {}});
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, ErrorCode::Io);
}
