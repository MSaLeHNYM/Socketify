// Unit tests for the Response builder.

#include "socketify/response.h"

#include <gtest/gtest.h>

using namespace socketify;

TEST(Response, SendSetsBodyAndEnds) {
    Response res;
    EXPECT_TRUE(res.send("hello"));
    EXPECT_TRUE(res.ended());
    EXPECT_EQ(res.body_view(), "hello");
    EXPECT_EQ(res.headers().find("Content-Type")->second, "text/plain; charset=utf-8");
}

TEST(Response, SendRespectsExplicitContentType) {
    Response res;
    res.set_content_type("application/xml");
    res.send("<x/>");
    EXPECT_EQ(res.headers().find("Content-Type")->second, "application/xml");
}

TEST(Response, SendAfterEndFails) {
    Response res;
    res.send("first");
    EXPECT_FALSE(res.send("second"));
    EXPECT_EQ(res.body_view(), "first");
}

TEST(Response, JsonSerializes) {
    Response res;
    res.json({{"a", 1}, {"b", "two"}});
    EXPECT_TRUE(res.ended());
    EXPECT_EQ(res.headers().find("Content-Type")->second, "application/json; charset=utf-8");
    auto j = nlohmann::json::parse(res.body_view());
    EXPECT_EQ(j["a"], 1);
    EXPECT_EQ(j["b"], "two");
}

TEST(Response, WriteAccumulatesUntilEnd) {
    Response res;
    EXPECT_TRUE(res.write("part1 "));
    EXPECT_TRUE(res.write("part2"));
    EXPECT_FALSE(res.ended());
    res.end();
    EXPECT_TRUE(res.ended());
    EXPECT_EQ(res.body_view(), "part1 part2");
    EXPECT_FALSE(res.write("more"));
}

TEST(Response, MultipleSetCookieHeaders) {
    Response res;
    res.set_cookie("a=1; Path=/");
    res.set_cookie(Cookie("b", "2").http_only());
    ASSERT_EQ(res.set_cookies().size(), 2u);
    EXPECT_EQ(res.set_cookies()[0], "a=1; Path=/");
    EXPECT_NE(res.set_cookies()[1].find("b=2"), std::string::npos);
    EXPECT_NE(res.set_cookies()[1].find("HttpOnly"), std::string::npos);
}

TEST(Response, Redirect) {
    Response res;
    res.redirect("/login");
    EXPECT_TRUE(res.ended());
    EXPECT_EQ(res.status_code(), 302);
    EXPECT_EQ(res.headers().find("Location")->second, "/login");
}

TEST(Response, RedirectCustomCode) {
    Response res;
    res.redirect("/perm", 301);
    EXPECT_EQ(res.status_code(), 301);
}

TEST(Response, SendStatus) {
    Response res;
    res.send_status(Status::NotFound);
    EXPECT_EQ(res.status_code(), 404);
    EXPECT_EQ(res.body_view(), "Not Found\n");
}

TEST(Response, SendFileMissingReturnsFalse) {
    Response res;
    EXPECT_FALSE(res.send_file("/no/such/file/anywhere.txt"));
    EXPECT_FALSE(res.ended());
}

TEST(Response, StatusChaining) {
    Response res;
    res.status(Status::Created).set_header("X-Custom", "v").send("done");
    EXPECT_EQ(res.status_code(), 201);
    EXPECT_EQ(res.headers().find("X-Custom")->second, "v");
}
