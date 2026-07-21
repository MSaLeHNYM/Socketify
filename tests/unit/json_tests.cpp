#include "socketify/json.h"

#include <gtest/gtest.h>

using namespace socketify::json_util;

TEST(JsonParse, ValidAndInvalid) {
    auto j = parse(R"({"a":1})");
    ASSERT_TRUE(j.has_value());
    EXPECT_EQ((*j)["a"].get<int>(), 1);
    EXPECT_FALSE(parse("").has_value());
    EXPECT_FALSE(parse("{bad").has_value());
    EXPECT_EQ(parse_or("bad", nlohmann::json{{"x", 1}})["x"], 1);
}

TEST(JsonPath, DottedAccess) {
    nlohmann::json doc = {{"user", {{"name", "Ada"}, {"age", 36}}}};
    EXPECT_TRUE(has(doc, "user.name"));
    EXPECT_EQ(get<std::string>(doc, "user.name").value_or(""), "Ada");
    EXPECT_EQ(get_or<int>(doc, "user.age", 0), 36);
    EXPECT_EQ(get_or<int>(doc, "user.missing", 99), 99);
    EXPECT_THROW(require<std::string>(doc, "user.missing"), Error);
    try {
        require<int>(doc, "user.name");
        FAIL() << "expected type error";
    } catch (const Error& e) {
        EXPECT_EQ(e.path(), "user.name");
    }
}
