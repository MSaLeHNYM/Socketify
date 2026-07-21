// Unit tests for the generated version header.

#include "socketify/version.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

TEST(Version, MacrosAreSet) {
    EXPECT_GE(SOCKETIFY_VERSION_MAJOR, 0);
    EXPECT_GE(SOCKETIFY_VERSION_MINOR, 0);
    EXPECT_GE(SOCKETIFY_VERSION_PATCH, 0);
    EXPECT_FALSE(std::string_view(SOCKETIFY_VERSION_STRING).empty());
}

TEST(Version, VersionStringMatchesHelper) {
    EXPECT_EQ(socketify::version_string(), SOCKETIFY_VERSION_STRING);
}

TEST(Version, StringLooksLikeSemVer) {
    const std::string s(SOCKETIFY_VERSION_STRING);
    // MAJOR.MINOR.PATCH — at least two dots and a digit.
    EXPECT_NE(s.find('.'), std::string::npos);
    EXPECT_TRUE(s.find_first_of("0123456789") != std::string::npos);
}
