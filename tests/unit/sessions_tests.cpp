// Unit tests for sessions: ServerStore, SignedCookie, JWT, rolling TTL.

#include "socketify/sessions.h"
#include "socketify/cookies.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <thread>

using namespace socketify;

namespace {

constexpr const char* kSecret = "unit-test-secret-key-32-bytes-!!";

Request make_req() {
    Request r;
    r.set_method(Method::GET);
    r.set_path("/x");
    return r;
}

std::string session_cookie_value(const Response& res, const std::string& name = "sid") {
    for (const auto& sc : res.set_cookies()) {
        if (sc.rfind(name + "=", 0) == 0) {
            auto semi = sc.find(';');
            return sc.substr(name.size() + 1,
                             semi == std::string::npos ? std::string::npos
                                                       : semi - name.size() - 1);
        }
    }
    return {};
}

bool cookie_expired(const Response& res, const std::string& name = "sid") {
    for (const auto& sc : res.set_cookies()) {
        if (sc.rfind(name + "=", 0) == 0 && sc.find("Max-Age=0") != std::string::npos)
            return true;
    }
    return false;
}

} // namespace

TEST(Sessions, ThrowsOnEmptySecret) {
    EXPECT_THROW(sessions::middleware(sessions::Options{}), std::invalid_argument);
}

TEST(Sessions, CreatesSessionAndSetsSignedCookie) {
    sessions::Options o;
    o.secret = kSecret;
    auto mw = sessions::middleware(o);

    auto req = make_req();
    Response res;
    mw(req, res, [&] {
        auto s = sessions::get(req);
        ASSERT_NE(s, nullptr);
        EXPECT_TRUE(s->is_new());
        s->set("user", "alice");
    });

    std::string cookie = session_cookie_value(res);
    ASSERT_FALSE(cookie.empty());
    EXPECT_NE(cookie.find('.'), std::string::npos);
}

TEST(Sessions, RestoresSessionFromValidCookie) {
    sessions::Options o;
    o.secret = kSecret;
    o.store = std::make_shared<sessions::MemoryStore>();
    auto mw = sessions::middleware(o);

    auto req1 = make_req();
    Response res1;
    mw(req1, res1, [&] { sessions::get(req1)->set("user", "bob"); });
    std::string cookie = session_cookie_value(res1);
    ASSERT_FALSE(cookie.empty());

    auto req2 = make_req();
    req2.mutable_cookies()["sid"] = cookie;
    Response res2;
    mw(req2, res2, [&] {
        auto s = sessions::get(req2);
        ASSERT_NE(s, nullptr);
        EXPECT_FALSE(s->is_new());
        EXPECT_EQ(s->get("user"), "bob");
    });
}

TEST(Sessions, RejectsTamperedCookie) {
    sessions::Options o;
    o.secret = kSecret;
    o.store = std::make_shared<sessions::MemoryStore>();
    auto mw = sessions::middleware(o);

    auto req1 = make_req();
    Response res1;
    mw(req1, res1, [&] { sessions::get(req1)->set("user", "carol"); });
    std::string cookie = session_cookie_value(res1);
    cookie[0] = (cookie[0] == 'a') ? 'b' : 'a';

    auto req2 = make_req();
    req2.mutable_cookies()["sid"] = cookie;
    Response res2;
    mw(req2, res2, [&] {
        auto s = sessions::get(req2);
        ASSERT_NE(s, nullptr);
        EXPECT_TRUE(s->is_new());
        EXPECT_FALSE(s->has("user"));
    });
}

TEST(Sessions, DestroyExpiresCookieAndStoreEntry) {
    auto store = std::make_shared<sessions::MemoryStore>();
    sessions::Options o;
    o.secret = kSecret;
    o.store = store;
    auto mw = sessions::middleware(o);

    auto req1 = make_req();
    Response res1;
    mw(req1, res1, [&] { sessions::get(req1)->set("k", "v"); });
    std::string cookie = session_cookie_value(res1);
    std::string id = cookie.substr(0, cookie.rfind('.'));
    EXPECT_TRUE(store->load(id).has_value());

    auto req2 = make_req();
    req2.mutable_cookies()["sid"] = cookie;
    Response res2;
    mw(req2, res2, [&] { sessions::get(req2)->destroy(); });

    EXPECT_FALSE(store->load(id).has_value());
    EXPECT_TRUE(cookie_expired(res2));
}

TEST(Sessions, RollingExtendsStoreTtl) {
    auto store = std::make_shared<sessions::MemoryStore>();
    sessions::Options o;
    o.secret = kSecret;
    o.store = store;
    o.ttl = std::chrono::seconds(1);
    o.rolling = true;
    auto mw = sessions::middleware(o);

    auto req1 = make_req();
    Response res1;
    mw(req1, res1, [&] { sessions::get(req1)->set("u", 1); });
    std::string cookie = session_cookie_value(res1);

    // Touch without dirtying — rolling must re-save.
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    auto req2 = make_req();
    req2.mutable_cookies()["sid"] = cookie;
    Response res2;
    mw(req2, res2, [&] {
        EXPECT_EQ(sessions::get(req2)->get("u"), 1);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    // Without rolling, store would have expired (~1.1s since first save).
    // With rolling at 0.4s, expiry is ~1.4s from start — still alive.
    auto req3 = make_req();
    req3.mutable_cookies()["sid"] = cookie;
    Response res3;
    mw(req3, res3, [&] {
        auto s = sessions::get(req3);
        EXPECT_FALSE(s->is_new());
        EXPECT_EQ(s->get("u"), 1);
    });
}

TEST(Sessions, NoCookieForEmptyNewSession) {
    sessions::Options o;
    o.secret = kSecret;
    o.save_uninitialized = false;
    auto mw = sessions::middleware(o);

    auto req = make_req();
    Response res;
    mw(req, res, [&] { (void)sessions::get(req); });
    EXPECT_TRUE(session_cookie_value(res).empty());
}

TEST(Sessions, RegenerateChangesId) {
    auto store = std::make_shared<sessions::MemoryStore>();
    sessions::Options o;
    o.secret = kSecret;
    o.store = store;
    auto mw = sessions::middleware(o);

    auto req1 = make_req();
    Response res1;
    std::string id1;
    mw(req1, res1, [&] {
        auto s = sessions::get(req1);
        s->set("user", "x");
        id1 = s->id();
    });
    std::string cookie1 = session_cookie_value(res1);

    auto req2 = make_req();
    req2.mutable_cookies()["sid"] = cookie1;
    Response res2;
    std::string id2;
    mw(req2, res2, [&] {
        auto s = sessions::get(req2);
        s->regenerate();
        s->set("user", "x");
        id2 = s->id(); // still old until middleware finishes — check cookie instead
    });
    std::string cookie2 = session_cookie_value(res2);
    ASSERT_FALSE(cookie2.empty());
    std::string new_id = cookie2.substr(0, cookie2.rfind('.'));
    EXPECT_NE(new_id, id1);
    EXPECT_FALSE(store->load(id1).has_value());
    EXPECT_TRUE(store->load(new_id).has_value());
}

TEST(Sessions, SignedCookieStrategy) {
    sessions::Options o;
    o.secret = kSecret;
    o.strategy = sessions::Strategy::SignedCookie;
    auto mw = sessions::middleware(o);

    auto req1 = make_req();
    Response res1;
    mw(req1, res1, [&] { sessions::get(req1)->set("role", "admin"); });
    std::string cookie = session_cookie_value(res1);
    ASSERT_FALSE(cookie.empty());

    auto req2 = make_req();
    req2.mutable_cookies()["sid"] = cookie;
    Response res2;
    mw(req2, res2, [&] {
        EXPECT_EQ(sessions::get(req2)->get("role"), "admin");
    });
}

TEST(Sessions, JwtCookieStrategy) {
    sessions::Options o;
    o.secret = kSecret;
    o.strategy = sessions::Strategy::JWT;
    o.jwt_transport = sessions::JwtTransport::Cookie;
    o.jwt_issuer = "socketify-tests";
    auto mw = sessions::middleware(o);

    auto req1 = make_req();
    Response res1;
    mw(req1, res1, [&] { sessions::get(req1)->set("uid", 42); });
    std::string cookie = session_cookie_value(res1);
    ASSERT_FALSE(cookie.empty());
    EXPECT_EQ(std::count(cookie.begin(), cookie.end(), '.'), 2);

    auto req2 = make_req();
    req2.mutable_cookies()["sid"] = cookie;
    Response res2;
    mw(req2, res2, [&] {
        EXPECT_EQ(sessions::get(req2)->get("uid"), 42);
    });
}

TEST(Sessions, JwtBearerStrategy) {
    sessions::Options o;
    o.secret = kSecret;
    o.strategy = sessions::Strategy::JWT;
    o.jwt_transport = sessions::JwtTransport::Bearer;
    auto mw = sessions::middleware(o);

    auto req1 = make_req();
    Response res1;
    mw(req1, res1, [&] { sessions::get(req1)->set("uid", 7); });
    auto it = res1.headers().find("X-Access-Token");
    ASSERT_NE(it, res1.headers().end());
    std::string token = it->second;

    auto req2 = make_req();
    req2.mutable_headers()["Authorization"] = "Bearer " + token;
    Response res2;
    mw(req2, res2, [&] {
        EXPECT_EQ(sessions::get(req2)->get("uid"), 7);
    });
}

TEST(JwtHelpers, EncodeDecodeRoundTrip) {
    auto token = sessions::jwt::encode(kSecret, {{"sub", "alice"}, {"data", {{"k", "v"}}}},
                                       std::chrono::seconds(60));
    auto claims = sessions::jwt::decode(kSecret, token);
    ASSERT_TRUE(claims.has_value());
    EXPECT_EQ((*claims)["sub"], "alice");
    EXPECT_EQ((*claims)["data"]["k"], "v");
}

TEST(JwtHelpers, RejectsBadSignature) {
    auto token = sessions::jwt::encode(kSecret, {{"sub", "x"}}, std::chrono::seconds(60));
    token.back() = (token.back() == 'A') ? 'B' : 'A';
    EXPECT_FALSE(sessions::jwt::decode(kSecret, token).has_value());
}

TEST(JwtHelpers, RejectsExpired) {
    auto token = sessions::jwt::encode(kSecret, {{"sub", "x"}}, std::chrono::seconds(0));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(sessions::jwt::decode(kSecret, token).has_value());
}

TEST(MemoryStore, TtlExpiry) {
    sessions::MemoryStore store;
    store.save("id1", {{"a", 1}}, std::chrono::seconds(0));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_FALSE(store.load("id1").has_value());

    store.save("id2", {{"b", 2}}, std::chrono::seconds(60));
    EXPECT_TRUE(store.load("id2").has_value());
}
