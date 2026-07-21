// Unit tests for logging levels and request-logging middleware.

#include "socketify/logging.h"
#include "socketify/request.h"
#include "socketify/response.h"
#include "socketify/http.h"

#include <gtest/gtest.h>

#include <mutex>
#include <string>
#include <utility>
#include <vector>

using namespace socketify;

namespace {

struct Captured {
    logging::Level level{};
    std::string message;
};

std::mutex g_cap_mu;
std::vector<Captured> g_captured;

void install_capture_sink() {
    g_captured.clear();
    logging::set_sink([](logging::Level lvl, std::string_view msg) {
        std::lock_guard lock(g_cap_mu);
        g_captured.push_back({lvl, std::string(msg)});
    });
}

void restore_defaults() {
    logging::set_sink(nullptr);
    logging::set_level(logging::Level::Info);
    std::lock_guard lock(g_cap_mu);
    g_captured.clear();
}

Request make_req(std::string path = "/x", std::string ip = "203.0.113.7") {
    Request r;
    r.set_method(Method::GET);
    r.set_path(std::move(path));
    r.set_target(r.path().empty() ? "/" : std::string(r.path()));
    r.set_version("HTTP/1.1");
    r.set_remote_ip(std::move(ip));
    return r;
}

} // namespace

class LoggingTest : public ::testing::Test {
protected:
    void SetUp() override { install_capture_sink(); }
    void TearDown() override { restore_defaults(); }
};

TEST_F(LoggingTest, ThresholdFiltersTraceWhenInfo) {
    logging::set_level(logging::Level::Info);
    logging::trace("should not appear");
    logging::info("hello {}", 42);
    logging::fatal("boom");

    ASSERT_EQ(g_captured.size(), 2u);
    EXPECT_EQ(g_captured[0].level, logging::Level::Info);
    EXPECT_EQ(g_captured[0].message, "hello 42");
    EXPECT_EQ(g_captured[1].level, logging::Level::Fatal);
    EXPECT_EQ(g_captured[1].message, "boom");
}

TEST_F(LoggingTest, MiddlewareIncludesIpAndInfoFor2xx) {
    logging::set_level(logging::Level::Info);
    auto mw = logging::middleware();
    auto req = make_req("/ok");
    Response res;
    mw(req, res, [&] { res.status(200).send("hi"); });

    ASSERT_EQ(g_captured.size(), 1u);
    EXPECT_EQ(g_captured[0].level, logging::Level::Info);
    EXPECT_NE(g_captured[0].message.find("203.0.113.7"), std::string::npos);
    EXPECT_NE(g_captured[0].message.find("GET"), std::string::npos);
    EXPECT_NE(g_captured[0].message.find("/ok"), std::string::npos);
    EXPECT_NE(g_captured[0].message.find("200"), std::string::npos);
}

TEST_F(LoggingTest, MiddlewareWarnFor404) {
    logging::set_level(logging::Level::Info);
    auto mw = logging::middleware();
    auto req = make_req("/missing");
    Response res;
    mw(req, res, [&] { res.status(404).send("nope"); });

    ASSERT_EQ(g_captured.size(), 1u);
    EXPECT_EQ(g_captured[0].level, logging::Level::Warn);
    EXPECT_NE(g_captured[0].message.find("203.0.113.7"), std::string::npos);
    EXPECT_NE(g_captured[0].message.find("404"), std::string::npos);
}

TEST_F(LoggingTest, MiddlewareErrorFor500) {
    logging::set_level(logging::Level::Info);
    auto mw = logging::middleware();
    auto req = make_req("/boom");
    Response res;
    mw(req, res, [&] { res.status(500).send("err"); });

    ASSERT_EQ(g_captured.size(), 1u);
    EXPECT_EQ(g_captured[0].level, logging::Level::Error);
    EXPECT_NE(g_captured[0].message.find("500"), std::string::npos);
}

TEST_F(LoggingTest, ThresholdDropsSuccessfulWhenErrorOnly) {
    logging::set_level(logging::Level::Error);
    auto mw = logging::middleware();
    auto req = make_req();
    Response res;
    mw(req, res, [&] { res.status(200).send("ok"); });
    EXPECT_TRUE(g_captured.empty());

    mw(req, res, [&] {
        res = Response{};
        res.status(500).send("fail");
    });
    ASSERT_EQ(g_captured.size(), 1u);
    EXPECT_EQ(g_captured[0].level, logging::Level::Error);
}

TEST_F(LoggingTest, TrustProxyUsesXForwardedFor) {
    logging::set_level(logging::Level::Info);
    logging::Options opts;
    opts.trust_proxy = true;
    auto mw = logging::middleware(opts);
    auto req = make_req("/p", "10.0.0.1");
    req.mutable_headers()["X-Forwarded-For"] = "198.51.100.9, 10.0.0.1";
    Response res;
    mw(req, res, [&] { res.status(200).send("ok"); });

    ASSERT_EQ(g_captured.size(), 1u);
    EXPECT_NE(g_captured[0].message.find("198.51.100.9"), std::string::npos);
    EXPECT_EQ(g_captured[0].message.find("10.0.0.1"), std::string::npos);
}

TEST_F(LoggingTest, DebugExtrasAppendRidAndUa) {
    logging::set_level(logging::Level::Debug);
    auto mw = logging::middleware();
    auto req = make_req();
    req.mutable_headers()[std::string(H_XRequestId)] = "abc-123";
    req.mutable_headers()["User-Agent"] =
        "Mozilla/5.0 (compatible; SocketifyTest/1.0; extra-padding-here)";
    Response res;
    mw(req, res, [&] { res.status(200).send("ok"); });

    ASSERT_EQ(g_captured.size(), 1u);
    EXPECT_NE(g_captured[0].message.find("rid=abc-123"), std::string::npos);
    EXPECT_NE(g_captured[0].message.find("ua="), std::string::npos);
    EXPECT_NE(g_captured[0].message.find("..."), std::string::npos);
}
