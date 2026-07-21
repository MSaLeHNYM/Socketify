// Unit tests for Pulse core enhancements, pulse_easy, and pulse_media.

#include "socketify/pulse.h"
#include "socketify/pulse_easy.h"
#include "socketify/pulse_media.h"
#include "socketify/detail/pulse_impl.h"

#include <gtest/gtest.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

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

namespace {

Channel make_channel() {
    return Channel(std::make_shared<Channel::Impl>());
}

void fire_close(Channel& ch) {
    auto impl = ch.impl();
    if (!impl) return;
    std::vector<CloseHandler> cbs;
    {
        std::lock_guard<std::mutex> lk(impl->mu);
        if (impl->close_fired) return;
        impl->close_fired = true;
        impl->closed = true;
        cbs = impl->on_close;
    }
    for (auto& cb : cbs) {
        if (cb) cb(ch, CloseCode::Normal, {});
    }
}

} // namespace

TEST(PulseEasy, AdoptRetainsHandlersAfterConnectionDropped) {
    App app;
    auto ch = make_channel();
    bool hit = false;
    {
        auto conn = app.adopt(ch);
        conn.on("send", [&](Connection&, const json&) { hit = true; });
    }
    EXPECT_TRUE(app.is_live(ch));

    TextHandler th;
    {
        std::lock_guard<std::mutex> lk(ch.impl()->mu);
        th = ch.impl()->on_text;
    }
    ASSERT_TRUE(static_cast<bool>(th));
    th(ch, R"({"type":"send","data":{"x":1}})");
    EXPECT_TRUE(hit);
}

TEST(PulseEasy, OnCloseReleasesAndIsIdempotent) {
    App app;
    auto ch = make_channel();
    int closes = 0;
    auto conn = app.adopt(ch);
    conn.on_close([&](Connection&, CloseCode, std::string_view) { ++closes; });
    EXPECT_TRUE(app.is_live(ch));

    fire_close(ch);
    EXPECT_EQ(closes, 1);
    EXPECT_FALSE(app.is_live(ch));

    app.release(ch); // idempotent
    EXPECT_FALSE(app.is_live(ch));
    fire_close(ch); // close_fired — no second user callback
    EXPECT_EQ(closes, 1);
}

TEST(PulseEasy, MediaLikeChannelOnCloseStillReleases) {
    App app;
    auto ch = make_channel();
    auto conn = app.adopt(ch);
    bool user = false;
    conn.on_close([&](Connection&, CloseCode, std::string_view) { user = true; });

    // pulse_media::attach appends a Channel close hook; App release must remain.
    bool media = false;
    ch.on_close([&](Channel&, CloseCode, std::string_view) { media = true; });

    fire_close(ch);
    EXPECT_TRUE(user);
    EXPECT_TRUE(media);
    EXPECT_FALSE(app.is_live(ch));
}

TEST(PulseEasy, BroadcastWhilePeerDisconnects) {
    ::socketify::pulse::Hub hub;
    App app(&hub);
    auto a = make_channel();
    auto b = make_channel();
    auto ca = app.adopt(a);
    auto cb = app.adopt(b);
    ca.join("room");
    cb.join("room");

    std::atomic<int> closes{0};
    ca.on_close([&](Connection& c, CloseCode, std::string_view) {
        ++closes;
        hub.leave_all(c.channel());
        hub.broadcast_text("room", R"({"type":"bye"})");
        hub.broadcast_text(R"({"type":"presence"})");
    });

    std::thread t1([&] {
        for (int i = 0; i < 50; ++i) hub.broadcast_text("room", "x");
    });
    std::thread t2([&] { fire_close(a); });
    t1.join();
    t2.join();

    EXPECT_EQ(closes.load(), 1);
    EXPECT_FALSE(app.is_live(a));
    EXPECT_TRUE(app.is_live(b));
    app.release(b);
}

TEST(PulseCore, OnCloseChainsMultipleHandlers) {
    auto ch = make_channel();
    int n = 0;
    ch.on_close([&](Channel&, CloseCode, std::string_view) { n += 1; });
    ch.on_close([&](Channel&, CloseCode, std::string_view) { n += 10; });
    fire_close(ch);
    EXPECT_EQ(n, 11);
}
