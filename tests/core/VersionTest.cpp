#include "core/util/Version.hpp"

#include <gtest/gtest.h>

using fdss::core::Version;

TEST(VersionTest, FormatsMajorMinorPatch) {
    EXPECT_EQ(fdss::core::toString(Version{1, 2, 3}), "1.2.3");
}

TEST(VersionTest, FormatsZeroVersion) { EXPECT_EQ(fdss::core::toString(Version{}), "0.0.0"); }

TEST(VersionTest, FormatsMultiDigitParts) {
    EXPECT_EQ(fdss::core::toString(Version{10, 0, 42}), "10.0.42");
}

TEST(VersionTest, CurrentVersionMatchesProjectVersion) {
    EXPECT_EQ(fdss::core::toString(fdss::core::currentVersion()), FDSS_EXPECTED_VERSION);
}
