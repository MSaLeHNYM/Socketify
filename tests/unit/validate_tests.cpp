#include "socketify/validate.h"

#include <gtest/gtest.h>

using namespace socketify::validate;

TEST(Validate, RequiredAndEmail) {
    const Schema schema = {
        field("email").required().email(),
        field("name").required().string().min(1),
    };
    auto bad = validate(nlohmann::json{{"email", "nope"}, {"name", ""}}, schema);
    EXPECT_FALSE(bad.ok);
    EXPECT_GE(bad.errors.size(), 1u);
    auto j = bad.errors_json();
    EXPECT_TRUE(j.contains("errors"));

    auto good = validate(nlohmann::json{{"email", "a@b.c"}, {"name", "Ada"}}, schema);
    EXPECT_TRUE(good.ok);
}

TEST(Validate, OneOfAndInteger) {
    const Schema schema = {
        field("role").required().one_of({"user", "admin"}),
        field("age").integer().min(13).max(120),
    };
    EXPECT_FALSE(validate({{"role", "guest"}, {"age", 10}}, schema).ok);
    EXPECT_TRUE(validate({{"role", "admin"}, {"age", 30}}, schema).ok);
}
