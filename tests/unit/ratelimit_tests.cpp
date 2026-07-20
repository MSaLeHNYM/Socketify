// Unit tests for the token-bucket rate limiter.

#include "socketify/ratelimit.h"

#include <gtest/gtest.h>

using namespace socketify;

namespace {

Request req_from(std::string ip) {
    Request r;
    r.set_method(Method::GET);
    r.set_path("/x");
    r.set_remote_ip(std::move(ip));
    return r;
}

} // namespace

TEST(RateLimit, AllowsWithinBudget) {
    ratelimit::Options o;
    o.capacity = 3;
    o.refill_per_second = 0.0; // no refill during the test
    auto mw = ratelimit::middleware(o);

    for (int i = 0; i < 3; ++i) {
        auto req = req_from("1.2.3.4");
        Response res;
        bool passed = false;
        mw(req, res, [&] { passed = true; });
        EXPECT_TRUE(passed) << "request " << i;
    }
}

TEST(RateLimit, BlocksWhenExhausted) {
    ratelimit::Options o;
    o.capacity = 2;
    o.refill_per_second = 0.0;
    auto mw = ratelimit::middleware(o);

    for (int i = 0; i < 2; ++i) {
        auto req = req_from("1.2.3.4");
        Response res;
        mw(req, res, [] {});
    }
    auto req = req_from("1.2.3.4");
    Response res;
    bool passed = false;
    mw(req, res, [&] { passed = true; });
    EXPECT_FALSE(passed);
    EXPECT_EQ(res.status_code(), 429);
    EXPECT_FALSE(res.headers().find("Retry-After")->second.empty());
}

TEST(RateLimit, SeparateBucketsPerKey) {
    ratelimit::Options o;
    o.capacity = 1;
    o.refill_per_second = 0.0;
    auto mw = ratelimit::middleware(o);

    auto r1 = req_from("1.1.1.1");
    Response res1;
    bool p1 = false;
    mw(r1, res1, [&] { p1 = true; });
    EXPECT_TRUE(p1);

    // Different IP has its own budget.
    auto r2 = req_from("2.2.2.2");
    Response res2;
    bool p2 = false;
    mw(r2, res2, [&] { p2 = true; });
    EXPECT_TRUE(p2);

    // First IP is now exhausted.
    auto r3 = req_from("1.1.1.1");
    Response res3;
    bool p3 = false;
    mw(r3, res3, [&] { p3 = true; });
    EXPECT_FALSE(p3);
}

TEST(RateLimit, CustomKeyFunction) {
    ratelimit::Options o;
    o.capacity = 1;
    o.refill_per_second = 0.0;
    o.key_fn = [](const Request& r) { return std::string(r.header("X-Api-Key")); };
    auto mw = ratelimit::middleware(o);

    auto mk = [](std::string key) {
        Request r;
        r.set_method(Method::GET);
        if (!key.empty()) r.mutable_headers()["X-Api-Key"] = std::move(key);
        return r;
    };

    auto a1 = mk("alpha");
    Response res;
    bool passed = false;
    mw(a1, res, [&] { passed = true; });
    EXPECT_TRUE(passed);

    auto a2 = mk("alpha");
    Response res2;
    passed = false;
    mw(a2, res2, [&] { passed = true; });
    EXPECT_FALSE(passed);

    // Empty key -> exempt.
    auto e = mk("");
    Response res3;
    passed = false;
    mw(e, res3, [&] { passed = true; });
    EXPECT_TRUE(passed);
}

TEST(RateLimit, EmitsStandardHeaders) {
    ratelimit::Options o;
    o.capacity = 5;
    o.refill_per_second = 1.0;
    auto mw = ratelimit::middleware(o);

    auto req = req_from("9.9.9.9");
    Response res;
    mw(req, res, [] {});
    EXPECT_EQ(res.headers().find("RateLimit-Limit")->second, "5");
    EXPECT_EQ(res.headers().find("RateLimit-Remaining")->second, "4");
}
