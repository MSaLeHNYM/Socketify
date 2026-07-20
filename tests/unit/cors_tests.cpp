// Unit tests for the CORS middleware: simple requests, preflights,
// credentials and origin validation.

#include "socketify/cors.h"

#include <gtest/gtest.h>

using namespace socketify;

namespace {

Request make_req(Method m, std::string origin = "",
                 std::string acrm = "") {
    Request r;
    r.set_method(m);
    r.set_path("/x");
    if (!origin.empty()) r.mutable_headers()["Origin"] = std::move(origin);
    if (!acrm.empty()) r.mutable_headers()["Access-Control-Request-Method"] = std::move(acrm);
    return r;
}

std::string header_of(const Response& res, std::string_view key) {
    auto it = res.headers().find(std::string(key));
    return it == res.headers().end() ? "" : it->second;
}

} // namespace

TEST(Cors, NonCorsRequestPassesThrough) {
    auto mw = cors::middleware();
    auto req = make_req(Method::GET);
    Response res;
    bool next_called = false;
    mw(req, res, [&] { next_called = true; });
    EXPECT_TRUE(next_called);
    EXPECT_EQ(header_of(res, "Access-Control-Allow-Origin"), "");
}

TEST(Cors, WildcardOrigin) {
    auto mw = cors::middleware();
    auto req = make_req(Method::GET, "https://evil.com");
    Response res;
    mw(req, res, [] {});
    EXPECT_EQ(header_of(res, "Access-Control-Allow-Origin"), "*");
}

TEST(Cors, ExactOriginAllowed) {
    cors::CorsOptions o;
    o.allow_origin = "https://app.example.com";
    auto mw = cors::middleware(o);

    auto req = make_req(Method::GET, "https://app.example.com");
    Response res;
    mw(req, res, [] {});
    EXPECT_EQ(header_of(res, "Access-Control-Allow-Origin"), "https://app.example.com");
}

TEST(Cors, ExactOriginRejectedWhenDifferent) {
    cors::CorsOptions o;
    o.allow_origin = "https://app.example.com";
    auto mw = cors::middleware(o);

    auto req = make_req(Method::GET, "https://other.com");
    Response res;
    mw(req, res, [] {});
    EXPECT_EQ(header_of(res, "Access-Control-Allow-Origin"), "");
}

TEST(Cors, ReflectOriginAddsVary) {
    cors::CorsOptions o;
    o.reflect_origin = true;
    auto mw = cors::middleware(o);

    auto req = make_req(Method::GET, "https://site.com");
    Response res;
    mw(req, res, [] {});
    EXPECT_EQ(header_of(res, "Access-Control-Allow-Origin"), "https://site.com");
    EXPECT_NE(header_of(res, "Vary").find("Origin"), std::string::npos);
}

TEST(Cors, PreflightShortCircuitsWith204) {
    auto mw = cors::middleware();
    auto req = make_req(Method::OPTIONS, "https://site.com", "POST");
    Response res;
    bool next_called = false;
    mw(req, res, [&] { next_called = true; });
    EXPECT_FALSE(next_called);
    EXPECT_TRUE(res.ended());
    EXPECT_EQ(res.status_code(), 204);
    EXPECT_FALSE(header_of(res, "Access-Control-Allow-Methods").empty());
}

TEST(Cors, PreflightContinueCallsNext) {
    cors::CorsOptions o;
    o.preflight_continue = true;
    auto mw = cors::middleware(o);

    auto req = make_req(Method::OPTIONS, "https://site.com", "POST");
    Response res;
    bool next_called = false;
    mw(req, res, [&] { next_called = true; });
    EXPECT_TRUE(next_called);
}

TEST(Cors, CredentialsHeaderSet) {
    cors::CorsOptions o;
    o.allow_origin = "https://app.example.com";
    o.allow_credentials = true;
    auto mw = cors::middleware(o);

    auto req = make_req(Method::GET, "https://app.example.com");
    Response res;
    mw(req, res, [] {});
    EXPECT_EQ(header_of(res, "Access-Control-Allow-Credentials"), "true");
}

TEST(Cors, WildcardWithCredentialsOmitsHeader) {
    cors::CorsOptions o;
    o.allow_credentials = true; // "*" + credentials is invalid; no header sent
    auto mw = cors::middleware(o);

    auto req = make_req(Method::GET, "https://site.com");
    Response res;
    mw(req, res, [] {});
    EXPECT_EQ(header_of(res, "Access-Control-Allow-Origin"), "");
}

TEST(Cors, ExposeHeadersOnActualRequest) {
    cors::CorsOptions o;
    o.expose_headers = "X-Total-Count";
    auto mw = cors::middleware(o);

    auto req = make_req(Method::GET, "https://site.com");
    Response res;
    mw(req, res, [] {});
    EXPECT_EQ(header_of(res, "Access-Control-Expose-Headers"), "X-Total-Count");
}
