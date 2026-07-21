// Unit tests for Pulse core enhancements, pulse_easy, and pulse_media.

#include "socketify/pulse.h"
#include "socketify/pulse_easy.h"
#include "socketify/pulse_media.h"

#include <gtest/gtest.h>

using namespace socketify::pulse;
using namespace socketify::pulse_easy;
using namespace socketify::pulse_media;

TEST(PulseCore, OutboundFragmentationRoundTrip) {
    auto f1 = encode_frame(0x1, "hel", false);
    auto f2 = encode_frame(0x0, "lo", true);
    EXPECT_FALSE(f1.empty());
    EXPECT_FALSE(f2.empty());
    EXPECT_EQ(f1[0] & 0x80, 0); // not fin
    EXPECT_EQ(f2[0] & 0x80, 0x80); // fin
}

TEST(PulseCore, EncodeOnceBroadcastFrame) {
    auto frame = encode_frame(0x1, R"({"ok":true})");
    EXPECT_GT(frame.size(), 2u);
}

TEST(PulseEasy, EnvelopeParse) {
    auto env = envelope("chat", json{{"text", "hi"}});
    EXPECT_EQ(env["type"], "chat");
    auto parsed = parse_envelope(env.dump());
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->first, "chat");
    EXPECT_EQ(parsed->second["text"], "hi");
}

TEST(PulseEasy, InvalidEnvelopeReturnsNullopt) {
    EXPECT_FALSE(parse_envelope("not json").has_value());
    EXPECT_FALSE(parse_envelope(R"({"data":{}})").has_value());
}

TEST(PulseMedia, PackUnpackVoice) {
    const std::string pcm = "pcm-bytes-here";
    auto blob = pack(Kind::Voice, 1, 42, 1000, FrameFlags::None, pcm);
    auto frame = unpack(blob);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->kind, Kind::Voice);
    EXPECT_EQ(frame->stream_id, 1);
    EXPECT_EQ(frame->seq, 42u);
    EXPECT_EQ(frame->timestamp_us, 1000u);
    EXPECT_EQ(frame->payload, pcm);
}

TEST(PulseMedia, PackUnpackImageWithMime) {
    const std::string img = "jpeg-data";
    auto blob = pack(Kind::Image, 0, 1, 2000, FrameFlags::Last, img, "image/jpeg");
    auto frame = unpack(blob);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->mime, "image/jpeg");
    EXPECT_EQ(frame->payload, img);
    EXPECT_TRUE(frame->flags & FrameFlags::Last);
}

TEST(PulseMedia, RejectsBadMagic) {
    EXPECT_FALSE(unpack("XX").has_value());
    EXPECT_FALSE(unpack("PM").has_value());
}

TEST(PulseMedia, VideoKeyframeFlag) {
    auto blob = pack(Kind::Video, 2, 1, 3000, FrameFlags::KeyFrame, "h264");
    auto frame = unpack(blob);
    ASSERT_TRUE(frame.has_value());
    EXPECT_TRUE(frame->flags & FrameFlags::KeyFrame);
}
