#pragma once
/**
 * @file pulse_media.h
 * @brief Voice, video, and image streaming over Pulse binary frames.
 *
 * Application binary protocol (magic `PM`) carried inside Pulse binary messages.
 * Use with @ref pulse_easy "pulse_easy" or raw @ref pulse "pulse" channels.
 *
 * @code
 * pulse_media::Hub media(app.hub());
 * media.on_voice("call-1", [](pulse::Channel& from, const pulse_media::Frame& f) {
 *     play_pcm(f.payload);
 * });
 * media.attach(conn.channel());
 * media.send_voice("call-1", pcm_chunk);
 * @endcode
 */

#include "socketify/pulse.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace socketify::pulse_media {

/** @brief Media frame kind. */
enum class Kind : std::uint8_t {
    Voice = 1,
    Video = 2,
    Image = 3,
    ImageEnd = 4,
};

/** @brief Frame flags bitmask. */
enum FrameFlags : std::uint8_t {
    None = 0,
    KeyFrame = 1 << 0,
    Last = 1 << 1,
};

/** @brief Parsed media frame. */
struct Frame {
    Kind kind{Kind::Voice};
    std::uint16_t stream_id{0};
    std::uint32_t seq{0};
    std::uint64_t timestamp_us{0};
    std::uint8_t flags{0};
    std::string mime;
    std::string payload;
};

using VoiceHandler = std::function<void(pulse::Channel& from, const Frame& frame)>;
using VideoHandler = std::function<void(pulse::Channel& from, const Frame& frame)>;
using ImageHandler = std::function<void(pulse::Channel& from, const Frame& frame)>;

/** @brief Pack a media frame into binary wire format. */
std::string pack(Kind kind, std::uint16_t stream_id, std::uint32_t seq, std::uint64_t timestamp_us,
                 std::uint8_t flags, std::string_view payload, std::string_view mime = {});

/** @brief Unpack binary wire format; nullopt on invalid/truncated data. */
std::optional<Frame> unpack(std::string_view data);

/** @brief Current timestamp in microseconds (steady clock). */
std::uint64_t now_us();

/**
 * @brief Room-based media relay hub.
 *
 * Attach channels, register handlers per room, send voice/video/image to rooms.
 */
class Hub {
public:
    explicit Hub(pulse::Hub* rooms = nullptr);

    pulse::Hub& rooms();

    /** @brief Wire binary dispatch on @p ch (safe to call once per channel). */
    void attach(pulse::Channel ch);

    void join(std::string room, pulse::Channel ch);

    bool send_voice(std::string_view room, std::string_view pcm, std::uint16_t stream_id = 0,
                    std::uint8_t flags = FrameFlags::None);
    bool send_video(std::string_view room, std::string_view frame_data, bool keyframe = false,
                    std::uint16_t stream_id = 0);
    bool send_image(std::string_view room, std::string_view image_bytes,
                    std::string_view mime = "image/jpeg", std::uint8_t flags = FrameFlags::Last);

    /** @brief Chunked image upload (multiple binary frames). */
    bool begin_image(std::string_view room, std::string_view mime, std::uint16_t stream_id = 0);
    bool write_image(std::string_view room, std::string_view chunk, std::uint16_t stream_id = 0);
    bool end_image(std::string_view room, std::uint16_t stream_id = 0);

    void on_voice(std::string room, VoiceHandler fn);
    void on_video(std::string room, VideoHandler fn);
    void on_image(std::string room, ImageHandler fn);

private:
    struct RoomHandlers {
        VoiceHandler voice;
        VideoHandler video;
        ImageHandler image;
    };

    void dispatch_(pulse::Channel& from, std::string_view room, const Frame& frame);
    std::uint32_t next_seq_(const std::string& key);

    pulse::Hub owned_rooms_;
    pulse::Hub* rooms_;
    mutable std::mutex mu_;
    std::unordered_map<std::string, RoomHandlers> handlers_;
    std::unordered_map<void*, std::string> channel_rooms_; // Impl* -> room
    std::unordered_map<void*, bool> attached_;
    std::unordered_map<std::string, std::uint32_t> seq_counters_;
};

} // namespace socketify::pulse_media
