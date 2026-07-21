#include "socketify/cache.h"

#include <gtest/gtest.h>

#include <chrono>

using namespace socketify::cache;
using Clock = TtlCache::Clock;

TEST(Cache, TtlAndJson) {
    Clock::time_point now = Clock::now();
    TtlCache cache;
    cache.set_clock([&] { return now; });

    cache.set("a", "1", std::chrono::seconds(10));
    ASSERT_TRUE(cache.get("a").has_value());
    EXPECT_EQ(*cache.get("a"), "1");

    now += std::chrono::seconds(11);
    EXPECT_FALSE(cache.get("a").has_value());

    cache.set_json("j", nlohmann::json{{"n", 42}}, std::chrono::seconds(5));
    now += std::chrono::seconds(1);
    auto j = cache.get_json("j");
    ASSERT_TRUE(j.has_value());
    EXPECT_EQ((*j)["n"].get<int>(), 42);

    EXPECT_EQ(cache.purge_expired(), 0u);
    now += std::chrono::seconds(10);
    EXPECT_FALSE(cache.get("j").has_value());
}

TEST(Cache, EraseAndClear) {
    TtlCache cache;
    cache.set("x", "y");
    EXPECT_TRUE(cache.contains("x"));
    cache.erase("x");
    EXPECT_FALSE(cache.contains("x"));
    cache.set("a", "1");
    cache.set("b", "2");
    EXPECT_EQ(cache.size(), 2u);
    cache.clear();
    EXPECT_EQ(cache.size(), 0u);
}
