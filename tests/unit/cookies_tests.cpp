// Unit tests for cookie parsing and the Set-Cookie builder.

#include "socketify/cookies.h"

#include <gtest/gtest.h>

using namespace socketify;

TEST(CookieParse, BasicPairs) {
    std::unordered_map<std::string, std::string> out;
    cookies::parse_cookie_header("a=1; b=2; c=three", out);
    EXPECT_EQ(out.at("a"), "1");
    EXPECT_EQ(out.at("b"), "2");
    EXPECT_EQ(out.at("c"), "three");
}

TEST(CookieParse, SkipsMalformedPairs) {
    std::unordered_map<std::string, std::string> out;
    cookies::parse_cookie_header("=bad; ; good=1; alsobad", out);
    EXPECT_EQ(out.size(), 1u);
    EXPECT_EQ(out.at("good"), "1");
}

TEST(CookieParse, StripsQuotes) {
    std::unordered_map<std::string, std::string> out;
    cookies::parse_cookie_header("k=\"quoted value\"", out);
    EXPECT_EQ(out.at("k"), "quoted value");
}

TEST(CookieBuild, NameValueOnly) {
    EXPECT_EQ(Cookie("k", "v").to_string(), "k=v");
}

TEST(CookieBuild, AllAttributes) {
    auto s = Cookie("sid", "abc123")
                 .path("/")
                 .domain("example.com")
                 .max_age(3600)
                 .secure()
                 .http_only()
                 .same_site(SameSite::Strict)
                 .to_string();
    EXPECT_NE(s.find("sid=abc123"), std::string::npos);
    EXPECT_NE(s.find("Path=/"), std::string::npos);
    EXPECT_NE(s.find("Domain=example.com"), std::string::npos);
    EXPECT_NE(s.find("Max-Age=3600"), std::string::npos);
    EXPECT_NE(s.find("Secure"), std::string::npos);
    EXPECT_NE(s.find("HttpOnly"), std::string::npos);
    EXPECT_NE(s.find("SameSite=Strict"), std::string::npos);
}

TEST(CookieBuild, ExpiredHelper) {
    auto s = cookies::expired("sid");
    EXPECT_NE(s.find("sid="), std::string::npos);
    EXPECT_NE(s.find("Max-Age=0"), std::string::npos);
    EXPECT_NE(s.find("Expires=Thu, 01 Jan 1970"), std::string::npos);
}
