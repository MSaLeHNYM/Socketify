// Unit tests for detail::HttpParser: incremental input, bodies, chunked
// encoding, malformed messages and limits.

#include "socketify/detail/http_parser.h"

#include <gtest/gtest.h>

using namespace socketify;
using detail::HttpParser;
using detail::ParseState;

namespace {

// Feed the whole string at once.
std::size_t feed(HttpParser& p, std::string_view s) {
    return p.consume(s.data(), s.size());
}

} // namespace

TEST(HttpParser, ParsesSimpleGet) {
    HttpParser p;
    std::string req = "GET /hello HTTP/1.1\r\nHost: x\r\n\r\n";
    EXPECT_EQ(feed(p, req), req.size());
    ASSERT_TRUE(p.complete());
    EXPECT_EQ(p.method(), Method::GET);
    EXPECT_EQ(p.path(), "/hello");
    EXPECT_EQ(p.query_string(), "");
    EXPECT_EQ(p.version(), "HTTP/1.1");
    EXPECT_EQ(p.headers().at("Host"), "x");
}

TEST(HttpParser, SplitsPathAndQuery) {
    HttpParser p;
    feed(p, "GET /api/items?id=42&name=x HTTP/1.1\r\n\r\n");
    ASSERT_TRUE(p.complete());
    EXPECT_EQ(p.path(), "/api/items");
    EXPECT_EQ(p.query_string(), "id=42&name=x");
    EXPECT_EQ(p.target(), "/api/items?id=42&name=x");
}

TEST(HttpParser, ParsesByteByByte) {
    HttpParser p;
    std::string req = "POST /submit HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello";
    for (char c : req) {
        p.consume(&c, 1);
    }
    ASSERT_TRUE(p.complete());
    EXPECT_EQ(p.method(), Method::POST);
    EXPECT_EQ(p.body_view(), "hello");
}

TEST(HttpParser, ContentLengthBody) {
    HttpParser p;
    feed(p, "POST /x HTTP/1.1\r\nContent-Length: 11\r\n\r\nhello world");
    ASSERT_TRUE(p.complete());
    EXPECT_EQ(p.body_view(), "hello world");
    EXPECT_EQ(p.content_length(), 11u);
}

TEST(HttpParser, DoesNotConsumePipelinedBytes) {
    HttpParser p;
    std::string two = "GET /a HTTP/1.1\r\n\r\nGET /b HTTP/1.1\r\n\r\n";
    std::size_t used = feed(p, two);
    ASSERT_TRUE(p.complete());
    EXPECT_EQ(p.path(), "/a");
    EXPECT_LT(used, two.size());

    // Second request parses from the leftover.
    p.reset();
    std::string_view rest = std::string_view(two).substr(used);
    EXPECT_EQ(feed(p, rest), rest.size());
    ASSERT_TRUE(p.complete());
    EXPECT_EQ(p.path(), "/b");
}

TEST(HttpParser, ChunkedBody) {
    HttpParser p;
    feed(p,
         "POST /up HTTP/1.1\r\n"
         "Transfer-Encoding: chunked\r\n\r\n"
         "5\r\nhello\r\n"
         "6\r\n world\r\n"
         "0\r\n\r\n");
    ASSERT_TRUE(p.complete()) << p.error_message();
    EXPECT_EQ(p.body_view(), "hello world");
}

TEST(HttpParser, ChunkedWithExtensionsAndTrailers) {
    HttpParser p;
    feed(p,
         "POST /up HTTP/1.1\r\n"
         "Transfer-Encoding: chunked\r\n\r\n"
         "5;ext=1\r\nhello\r\n"
         "0\r\n"
         "X-Trailer: v\r\n"
         "\r\n");
    ASSERT_TRUE(p.complete()) << p.error_message();
    EXPECT_EQ(p.body_view(), "hello");
}

TEST(HttpParser, RejectsChunkedPlusContentLength) {
    HttpParser p;
    feed(p,
         "POST /x HTTP/1.1\r\n"
         "Transfer-Encoding: chunked\r\n"
         "Content-Length: 5\r\n\r\n");
    ASSERT_TRUE(p.error());
    EXPECT_EQ(p.error_status(), Status::BadRequest);
}

TEST(HttpParser, RejectsMalformedStartLine) {
    HttpParser p;
    feed(p, "GARBAGE\r\n\r\n");
    EXPECT_TRUE(p.error());
}

TEST(HttpParser, RejectsUnknownMethod) {
    HttpParser p;
    feed(p, "FROB /x HTTP/1.1\r\n\r\n");
    ASSERT_TRUE(p.error());
    EXPECT_EQ(p.error_status(), Status::NotImplemented);
}

TEST(HttpParser, RejectsBadVersion) {
    HttpParser p;
    feed(p, "GET /x HTTP/2.0\r\n\r\n");
    ASSERT_TRUE(p.error());
    EXPECT_EQ(static_cast<int>(p.error_status()), 505);
}

TEST(HttpParser, RejectsNegativeContentLength) {
    HttpParser p;
    feed(p, "POST /x HTTP/1.1\r\nContent-Length: -5\r\n\r\n");
    EXPECT_TRUE(p.error());
}

TEST(HttpParser, EnforcesHeaderLimit) {
    HttpParser p;
    p.set_limits(64, 1024);
    std::string req = "GET /x HTTP/1.1\r\nX-Big: " + std::string(200, 'a') + "\r\n\r\n";
    feed(p, req);
    ASSERT_TRUE(p.error());
    EXPECT_EQ(static_cast<int>(p.error_status()), 431);
}

TEST(HttpParser, EnforcesBodyLimit) {
    HttpParser p;
    p.set_limits(4096, 8);
    feed(p, "POST /x HTTP/1.1\r\nContent-Length: 100\r\n\r\n");
    ASSERT_TRUE(p.error());
    EXPECT_EQ(p.error_status(), Status::PayloadTooLarge);
}

TEST(HttpParser, EnforcesBodyLimitChunked) {
    HttpParser p;
    p.set_limits(4096, 8);
    feed(p,
         "POST /x HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n"
         "64\r\n");
    ASSERT_TRUE(p.error());
    EXPECT_EQ(p.error_status(), Status::PayloadTooLarge);
}

TEST(HttpParser, JoinsRepeatedHeaders) {
    HttpParser p;
    feed(p, "GET / HTTP/1.1\r\nX-K: a\r\nX-K: b\r\n\r\n");
    ASSERT_TRUE(p.complete());
    EXPECT_EQ(p.headers().at("X-K"), "a, b");
}

TEST(HttpParser, HeaderKeysAreCaseInsensitive) {
    HttpParser p;
    feed(p, "GET / HTTP/1.1\r\ncOnTeNt-TyPe: text/plain\r\n\r\n");
    ASSERT_TRUE(p.complete());
    EXPECT_EQ(p.headers().at("Content-Type"), "text/plain");
}

TEST(HttpParser, ResetAllowsReuse) {
    HttpParser p;
    feed(p, "GET /one HTTP/1.1\r\n\r\n");
    ASSERT_TRUE(p.complete());
    p.reset();
    feed(p, "POST /two HTTP/1.1\r\nContent-Length: 2\r\n\r\nok");
    ASSERT_TRUE(p.complete());
    EXPECT_EQ(p.path(), "/two");
    EXPECT_EQ(p.body_view(), "ok");
}
