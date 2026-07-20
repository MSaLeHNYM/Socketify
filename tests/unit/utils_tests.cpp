// Unit tests for detail utilities: url decoding, query parsing, crypto
// helpers, base64url, HTTP dates and the byte buffer.

#include "socketify/detail/buffer.h"
#include "socketify/detail/utils.h"
#include "socketify/request.h"

#include <gtest/gtest.h>

using namespace socketify;
namespace du = socketify::detail;

TEST(Utils, UrlDecodeBasic) {
    std::string out;
    EXPECT_TRUE(du::url_decode("/a%20b/c%2Fd", out));
    EXPECT_EQ(out, "/a b/c/d");
}

TEST(Utils, UrlDecodePlusAsSpace) {
    std::string out;
    EXPECT_TRUE(du::url_decode("a+b", out, true));
    EXPECT_EQ(out, "a b");
    EXPECT_TRUE(du::url_decode("a+b", out, false));
    EXPECT_EQ(out, "a+b");
}

TEST(Utils, UrlDecodeRejectsMalformed) {
    std::string out;
    EXPECT_FALSE(du::url_decode("%zz", out));
    EXPECT_FALSE(du::url_decode("%2", out));
    EXPECT_FALSE(du::url_decode("abc%", out));
}

TEST(Utils, ParseQueryString) {
    ParamMap q;
    du::parse_query_string("a=1&b=two%20words&c=&d", q);
    EXPECT_EQ(q.at("a"), "1");
    EXPECT_EQ(q.at("b"), "two words");
    EXPECT_EQ(q.at("c"), "");
    EXPECT_EQ(q.at("d"), "");
}

TEST(Utils, Sha256KnownVector) {
    // SHA-256("abc")
    auto d = du::sha256("abc");
    EXPECT_EQ(du::hex_encode(d.data(), d.size()),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(Utils, Sha256Empty) {
    auto d = du::sha256("");
    EXPECT_EQ(du::hex_encode(d.data(), d.size()),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(Utils, HmacSha256KnownVector) {
    // RFC 4231 test case 2: key="Jefe", data="what do ya want for nothing?"
    auto mac = du::hmac_sha256("Jefe", "what do ya want for nothing?");
    EXPECT_EQ(du::hex_encode(mac.data(), mac.size()),
              "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

TEST(Utils, Base64UrlRoundTrip) {
    std::string data = "any carnal pleasure.";
    auto enc = du::base64url_encode(reinterpret_cast<const unsigned char*>(data.data()),
                                    data.size());
    EXPECT_EQ(enc.find('+'), std::string::npos);
    EXPECT_EQ(enc.find('/'), std::string::npos);
    EXPECT_EQ(enc.find('='), std::string::npos);
    auto dec = du::base64url_decode(enc);
    ASSERT_TRUE(dec.has_value());
    EXPECT_EQ(*dec, data);
}

TEST(Utils, Base64UrlRejectsGarbage) {
    EXPECT_FALSE(du::base64url_decode("!!!").has_value());
}

TEST(Utils, ConstantTimeEqual) {
    EXPECT_TRUE(du::constant_time_equal("same", "same"));
    EXPECT_FALSE(du::constant_time_equal("same", "diff"));
    EXPECT_FALSE(du::constant_time_equal("short", "longer"));
}

TEST(Utils, HttpDateRoundTrip) {
    std::int64_t t = 784111777; // Sun, 06 Nov 1994 08:49:37 GMT
    auto s = du::http_date(t);
    EXPECT_EQ(s, "Sun, 06 Nov 1994 08:49:37 GMT");
    auto parsed = du::parse_http_date(s);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, t);
}

TEST(Utils, RandomTokenLengthAndUniqueness) {
    auto a = du::random_token(16);
    auto b = du::random_token(16);
    EXPECT_EQ(a.size(), 32u); // hex
    EXPECT_NE(a, b);
}

TEST(Buffer, AppendConsume) {
    du::Buffer b;
    b.append("hello ");
    b.append("world");
    EXPECT_EQ(b.view(), "hello world");
    b.consume(6);
    EXPECT_EQ(b.view(), "world");
    b.consume(999); // clamps
    EXPECT_TRUE(b.empty());
}

TEST(Buffer, CompactsLargeDeadPrefix) {
    du::Buffer b;
    std::string big(10000, 'x');
    b.append(big);
    b.consume(9000);
    b.append("tail");
    EXPECT_EQ(b.size(), 1004u);
    EXPECT_EQ(b.view().substr(1000), "tail");
}

TEST(CaseInsensitiveHeaderMap, Lookup) {
    HeaderMap h;
    h["Content-Type"] = "text/plain";
    EXPECT_EQ(h.find("content-type")->second, "text/plain");
    EXPECT_EQ(h.find("CONTENT-TYPE")->second, "text/plain");
}

TEST(Mime, KnownExtensions) {
    EXPECT_EQ(content_type_for_path("/a/b/index.html"), "text/html; charset=utf-8");
    EXPECT_EQ(content_type_for_path("app.wasm"), "application/wasm");
    EXPECT_EQ(content_type_for_path("font.woff2"), "font/woff2");
    EXPECT_EQ(content_type_for_path("noext"), "application/octet-stream");
}
