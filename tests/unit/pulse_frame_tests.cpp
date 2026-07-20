// Unit tests for Pulse framing and Sec-WebSocket-Accept.

#include "socketify/pulse.h"
#include "socketify/detail/utils.h"

#include <gtest/gtest.h>

using namespace socketify;
using namespace socketify::pulse;

TEST(PulseAccept, Rfc6455Example) {
    // RFC 6455 §1.3 / §4.2.2 example
    auto accept = accept_key("dGhlIHNhbXBsZSBub25jZQ==");
    EXPECT_EQ(accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST(PulseFrame, EncodeDecodeRoundTrip) {
    auto framed = encode_frame(0x1, "hello");
    // Client frames must be masked — build a masked version for decode_frame.
    ASSERT_GE(framed.size(), 2u);
    // Server frame is unmasked; forge a client frame:
    std::string client;
    client.push_back(static_cast<char>(0x81)); // fin + text
    client.push_back(static_cast<char>(0x85)); // mask + len 5
    unsigned char mask[4] = {0x12, 0x34, 0x56, 0x78};
    client.append(reinterpret_cast<char*>(mask), 4);
    const char* msg = "hello";
    for (int i = 0; i < 5; ++i) client.push_back(static_cast<char>(msg[i] ^ mask[i % 4]));

    auto dec = decode_frame(client, 1024);
    ASSERT_TRUE(dec.ok);
    EXPECT_EQ(dec.opcode, 0x1);
    EXPECT_TRUE(dec.fin);
    EXPECT_EQ(dec.payload, "hello");
    EXPECT_EQ(dec.bytes_consumed, client.size());
}

TEST(PulseFrame, RejectsUnmaskedClientFrame) {
    auto framed = encode_frame(0x1, "x");
    auto dec = decode_frame(framed, 1024);
    EXPECT_FALSE(dec.ok);
    EXPECT_TRUE(dec.protocol_error);
}

TEST(PulseFrame, NeedsMoreData) {
    auto dec = decode_frame(std::string_view("\x81", 1), 1024);
    EXPECT_FALSE(dec.ok);
    EXPECT_FALSE(dec.protocol_error);
    EXPECT_EQ(dec.bytes_consumed, 0u);
}

TEST(PulseSha1, KnownVector) {
    // SHA1("abc") = a9993e364706816aba3e25717850c26c9cd0d89d
    auto d = detail::sha1("abc");
    EXPECT_EQ(detail::hex_encode(d.data(), d.size()), "a9993e364706816aba3e25717850c26c9cd0d89d");
}
