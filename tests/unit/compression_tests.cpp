// Unit tests for compression negotiation and zlib round-trips.

#include "socketify/compression.h"

#include <gtest/gtest.h>
#include <zlib.h>

#include <cstring>

using namespace socketify;
namespace comp = socketify::compression;

TEST(Compression, NegotiatesGzip) {
    comp::Options o;
    EXPECT_EQ(comp::negotiate_accept_encoding("gzip, deflate", o), comp::Encoding::Gzip);
    EXPECT_EQ(comp::negotiate_accept_encoding("gzip", o), comp::Encoding::Gzip);
}

TEST(Compression, NegotiatesDeflateWhenGzipDisabled) {
    comp::Options o;
    o.enable_gzip = false;
    EXPECT_EQ(comp::negotiate_accept_encoding("gzip, deflate", o), comp::Encoding::Deflate);
}

TEST(Compression, NoneWhenNothingAcceptable) {
    comp::Options o;
    EXPECT_EQ(comp::negotiate_accept_encoding("br", o), comp::Encoding::None);
    EXPECT_EQ(comp::negotiate_accept_encoding("", o), comp::Encoding::None);
}

TEST(Compression, CompressibleTypes) {
    comp::Options o;
    EXPECT_TRUE(comp::is_compressible_type("text/html; charset=utf-8", o));
    EXPECT_TRUE(comp::is_compressible_type("application/json", o));
    EXPECT_FALSE(comp::is_compressible_type("image/png", o));
    EXPECT_FALSE(comp::is_compressible_type("video/mp4", o));
}

TEST(Compression, GzipRoundTrip) {
    std::string src(4096, 'a');
    std::string out;
    ASSERT_TRUE(comp::gzip_compress(src, out));
    ASSERT_LT(out.size(), src.size());
    // gzip magic bytes
    ASSERT_GE(out.size(), 2u);
    EXPECT_EQ(static_cast<unsigned char>(out[0]), 0x1f);
    EXPECT_EQ(static_cast<unsigned char>(out[1]), 0x8b);

    // Inflate back and compare.
    std::string round(src.size(), '\0');
    z_stream zs{};
    ASSERT_EQ(inflateInit2(&zs, 15 + 16), Z_OK);
    zs.next_in = reinterpret_cast<Bytef*>(out.data());
    zs.avail_in = static_cast<uInt>(out.size());
    zs.next_out = reinterpret_cast<Bytef*>(round.data());
    zs.avail_out = static_cast<uInt>(round.size());
    ASSERT_EQ(inflate(&zs, Z_FINISH), Z_STREAM_END);
    inflateEnd(&zs);
    round.resize(zs.total_out);
    EXPECT_EQ(round, src);
}

TEST(Compression, DeflateRoundTrip) {
    std::string src = "hello hello hello hello hello hello";
    std::string out;
    ASSERT_TRUE(comp::deflate_compress(src, out));

    std::string round(src.size() * 2, '\0');
    z_stream zs{};
    ASSERT_EQ(inflateInit(&zs), Z_OK);
    zs.next_in = reinterpret_cast<Bytef*>(out.data());
    zs.avail_in = static_cast<uInt>(out.size());
    zs.next_out = reinterpret_cast<Bytef*>(round.data());
    zs.avail_out = static_cast<uInt>(round.size());
    ASSERT_EQ(inflate(&zs, Z_FINISH), Z_STREAM_END);
    inflateEnd(&zs);
    round.resize(zs.total_out);
    EXPECT_EQ(round, src);
}
