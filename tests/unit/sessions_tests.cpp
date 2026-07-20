// Unit tests for sessions: signing, verification, store TTL, destroy.

#include "socketify/sessions.h"
#include "socketify/cookies.h"

#include <gtest/gtest.h>

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

// Extract the session cookie value from a response.
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
    // Format: <id>.<sig>
    EXPECT_NE(cookie.find('.'), std::string::npos);
}

TEST(Sessions, RestoresSessionFromValidCookie) {
    sessions::Options o;
    o.secret = kSecret;
    o.store = std::make_shared<sessions::MemoryStore>();
    auto mw = sessions::middleware(o);

    // First request: create.
    auto req1 = make_req();
    Response res1;
    mw(req1, res1, [&] { sessions::get(req1)->set("user", "bob"); });
    std::string cookie = session_cookie_value(res1);
    ASSERT_FALSE(cookie.empty());

    // Second request presents the cookie.
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

    // Flip a character in the id part.
    cookie[0] = (cookie[0] == 'a') ? 'b' : 'a';

    auto req2 = make_req();
    req2.mutable_cookies()["sid"] = cookie;
    Response res2;
    mw(req2, res2, [&] {
        auto s = sessions::get(req2);
        ASSERT_NE(s, nullptr);
        EXPECT_TRUE(s->is_new()); // fresh session, tampered cookie rejected
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
    // The response carries an expiring cookie.
    bool has_expired_cookie = false;
    for (const auto& sc : res2.set_cookies()) {
        if (sc.find("Max-Age=0") != std::string::npos) has_expired_cookie = true;
    }
    EXPECT_TRUE(has_expired_cookie);
}

TEST(MemoryStore, TtlExpiry) {
    sessions::MemoryStore store;
    store.save("id1", {{"a", 1}}, std::chrono::seconds(0));
    // Zero TTL: already expired.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_FALSE(store.load("id1").has_value());

    store.save("id2", {{"b", 2}}, std::chrono::seconds(60));
    EXPECT_TRUE(store.load("id2").has_value());
}
