/**
 * @file pulse_media.cpp
 * @brief Voice/video/image streaming over Pulse binary frames.
 */

#include "socketify/pulse_media.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace socketify::pulse_media {
namespace {

constexpr char kMagic0 = 'P';
constexpr char kMagic1 = 'M';
constexpr std::size_t kHeaderBase = 18;

void write_u16(char* p, std::uint16_t v) {
    p[0] = static_cast<char>((v >> 8) & 0xff);
    p[1] = static_cast<char>(v & 0xff);
}

void write_u32(char* p, std::uint32_t v) {
    p[0] = static_cast<char>((v >> 24) & 0xff);
    p[1] = static_cast<char>((v >> 16) & 0xff);
    p[2] = static_cast<char>((v >> 8) & 0xff);
    p[3] = static_cast<char>(v & 0xff);
}

void write_u64(char* p, std::uint64_t v) {
    for (int i = 7; i >= 0; --i) p[7 - i] = static_cast<char>((v >> (8 * i)) & 0xff);
}

std::uint16_t read_u16(const char* p) {
    return static_cast<std::uint16_t>((static_cast<unsigned char>(p[0]) << 8) |
                                      static_cast<unsigned char>(p[1]));
}

std::uint32_t read_u32(const char* p) {
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(p[0])) << 24) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(p[1])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(p[2])) << 8) |
           static_cast<unsigned char>(p[3]);
}

std::uint64_t read_u64(const char* p) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | static_cast<unsigned char>(p[i]);
    return v;
}

} // namespace

std::uint64_t now_us() {
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

std::string pack(Kind kind, std::uint16_t stream_id, std::uint32_t seq, std::uint64_t timestamp_us,
                 std::uint8_t flags, std::string_view payload, std::string_view mime) {
    const bool has_mime = (kind == Kind::Image || kind == Kind::ImageEnd);
    const std::uint16_t mime_len =
        has_mime ? static_cast<std::uint16_t>(std::min(mime.size(), std::size_t{65535})) : 0;
    const std::size_t total = kHeaderBase + (has_mime ? 2 + mime_len : 0) + payload.size();
    std::string out(total, '\0');
    char* h = out.data();
    h[0] = kMagic0;
    h[1] = kMagic1;
    h[2] = static_cast<char>(kind);
    write_u16(h + 3, stream_id);
    write_u32(h + 5, seq);
    write_u64(h + 9, timestamp_us);
    h[17] = static_cast<char>(flags);
    std::size_t off = kHeaderBase;
    if (has_mime) {
        write_u16(h + off, mime_len);
        off += 2;
        if (mime_len) std::memcpy(h + off, mime.data(), mime_len);
        off += mime_len;
    }
    if (!payload.empty()) std::memcpy(h + off, payload.data(), payload.size());
    return out;
}

std::optional<Frame> unpack(std::string_view data) {
    if (data.size() < kHeaderBase) return std::nullopt;
    if (data[0] != kMagic0 || data[1] != kMagic1) return std::nullopt;
    Frame f;
    f.kind = static_cast<Kind>(static_cast<unsigned char>(data[2]));
    f.stream_id = read_u16(data.data() + 3);
    f.seq = read_u32(data.data() + 5);
    f.timestamp_us = read_u64(data.data() + 9);
    f.flags = static_cast<std::uint8_t>(data[17]);
    std::size_t off = kHeaderBase;
    if (f.kind == Kind::Image || f.kind == Kind::ImageEnd) {
        if (data.size() < off + 2) return std::nullopt;
        const auto mime_len = read_u16(data.data() + off);
        off += 2;
        if (data.size() < off + mime_len) return std::nullopt;
        if (mime_len) f.mime.assign(data.data() + off, mime_len);
        off += mime_len;
    }
    if (off < data.size()) f.payload.assign(data.data() + off, data.size() - off);
    return f;
}

Hub::Hub(pulse::Hub* rooms) : rooms_(rooms ? rooms : &owned_rooms_) {}

pulse::Hub& Hub::rooms() { return *rooms_; }

std::uint32_t Hub::next_seq_(const std::string& key) { return ++seq_counters_[key]; }

void Hub::attach(pulse::Channel ch) {
    if (!ch.valid()) return;
    void* key = ch.impl().get();
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (attached_[key]) return;
        attached_[key] = true;
    }
    ch.on_binary([this](pulse::Channel& from, std::string_view data) {
        auto frame = unpack(data);
        if (!frame) return;
        std::string room;
        {
            std::lock_guard<std::mutex> lk(mu_);
            auto it = channel_rooms_.find(from.impl().get());
            if (it != channel_rooms_.end()) room = it->second;
        }
        if (room.empty()) return;
        dispatch_(from, room, *frame);
    });
    ch.on_close([this](pulse::Channel& c, pulse::CloseCode, std::string_view) {
        std::lock_guard<std::mutex> lk(mu_);
        channel_rooms_.erase(c.impl().get());
        attached_.erase(c.impl().get());
        rooms_->leave_all(c);
    });
}

void Hub::join(std::string room, pulse::Channel ch) {
    if (!ch.valid()) return;
    attach(ch);
    rooms_->join(room, ch);
    std::lock_guard<std::mutex> lk(mu_);
    channel_rooms_[ch.impl().get()] = std::move(room);
}

bool Hub::send_voice(std::string_view room, std::string_view pcm, std::uint16_t stream_id,
                     std::uint8_t flags) {
    const std::string key = std::string(room) + ":v:" + std::to_string(stream_id);
    const auto seq = next_seq_(key);
    const auto blob = pack(Kind::Voice, stream_id, seq, now_us(), flags, pcm);
    rooms_->broadcast_binary(room, blob);
    return true;
}

bool Hub::send_video(std::string_view room, std::string_view frame_data, bool keyframe,
                     std::uint16_t stream_id) {
    const std::string key = std::string(room) + ":vid:" + std::to_string(stream_id);
    const auto seq = next_seq_(key);
    const std::uint8_t flags = keyframe ? FrameFlags::KeyFrame : FrameFlags::None;
    const auto blob = pack(Kind::Video, stream_id, seq, now_us(), flags, frame_data);
    rooms_->broadcast_binary(room, blob);
    return true;
}

bool Hub::send_image(std::string_view room, std::string_view image_bytes, std::string_view mime,
                     std::uint8_t flags) {
    const std::string key = std::string(room) + ":img";
    const auto seq = next_seq_(key);
    const auto blob = pack(Kind::Image, 0, seq, now_us(), flags, image_bytes, mime);
    rooms_->broadcast_binary(room, blob);
    return true;
}

bool Hub::begin_image(std::string_view room, std::string_view mime, std::uint16_t stream_id) {
    const std::string key = std::string(room) + ":img:" + std::to_string(stream_id);
    const auto seq = next_seq_(key);
    const auto blob = pack(Kind::Image, stream_id, seq, now_us(), FrameFlags::None, {}, mime);
    rooms_->broadcast_binary(room, blob);
    return true;
}

bool Hub::write_image(std::string_view room, std::string_view chunk, std::uint16_t stream_id) {
    const std::string key = std::string(room) + ":img:" + std::to_string(stream_id);
    const auto seq = next_seq_(key);
    const auto blob = pack(Kind::Image, stream_id, seq, now_us(), FrameFlags::None, chunk);
    rooms_->broadcast_binary(room, blob);
    return true;
}

bool Hub::end_image(std::string_view room, std::uint16_t stream_id) {
    const std::string key = std::string(room) + ":img:" + std::to_string(stream_id);
    const auto seq = next_seq_(key);
    const auto blob =
        pack(Kind::ImageEnd, stream_id, seq, now_us(), FrameFlags::Last, {}, "image/jpeg");
    rooms_->broadcast_binary(room, blob);
    return true;
}

void Hub::on_voice(std::string room, VoiceHandler fn) {
    std::lock_guard<std::mutex> lk(mu_);
    handlers_[std::move(room)].voice = std::move(fn);
}

void Hub::on_video(std::string room, VideoHandler fn) {
    std::lock_guard<std::mutex> lk(mu_);
    handlers_[std::move(room)].video = std::move(fn);
}

void Hub::on_image(std::string room, ImageHandler fn) {
    std::lock_guard<std::mutex> lk(mu_);
    handlers_[std::move(room)].image = std::move(fn);
}

void Hub::dispatch_(pulse::Channel& from, std::string_view room, const Frame& frame) {
    RoomHandlers h;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = handlers_.find(std::string(room));
        if (it == handlers_.end()) return;
        h = it->second;
    }
    switch (frame.kind) {
        case Kind::Voice:
            if (h.voice) h.voice(from, frame);
            break;
        case Kind::Video:
            if (h.video) h.video(from, frame);
            break;
        case Kind::Image:
        case Kind::ImageEnd:
            if (h.image) h.image(from, frame);
            break;
    }
}

} // namespace socketify::pulse_media
