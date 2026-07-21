#include "socketify/config.h"

#include <gtest/gtest.h>

#include <cstdlib>

using namespace socketify::config;

TEST(Config, ParseEnvFile) {
    auto m = Config::parse_env(R"(
# comment
export PORT=8080
DEBUG=true
NAME="hello world"
)");
    EXPECT_EQ(m["PORT"], "8080");
    EXPECT_EQ(m["DEBUG"], "true");
    EXPECT_EQ(m["NAME"], "hello world");
}

TEST(Config, TypedAccess) {
    Config c;
    c.set("PORT", "9090").set("DEBUG", "yes").set("RATIO", "3.14");
    EXPECT_EQ(c.get_int("PORT").value_or(0), 9090);
    EXPECT_EQ(c.get_bool("DEBUG").value_or(false), true);
    EXPECT_NEAR(c.get_double("RATIO").value_or(0.0), 3.14, 0.001);
    EXPECT_THROW(c.require("MISSING"), Error);
}

TEST(Config, FromEnv) {
    setenv("SOCKETIFY_TEST_KEY", "value", 1);
    auto c = Config::from_env();
    EXPECT_EQ(c.get("SOCKETIFY_TEST_KEY").value_or(""), "value");
    unsetenv("SOCKETIFY_TEST_KEY");
}
