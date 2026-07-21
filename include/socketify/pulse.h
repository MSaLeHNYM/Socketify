#pragma once
/**
 * @file pulse.h
 * @brief Pulse — Socketify bidirectional realtime channels (RFC 6455).
 *
 * Higher-level helpers: `pulse_easy` (JSON events), `pulse_media` (voice/video/image).
 */

#include "socketify/request.h"
#include "socketify/response.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace socketify::pulse {

enum class CloseCode : std::uint16_t {
    Normal = 1000,
    GoingAway = 1001,
    ProtocolError = 1002,
    UnsupportedData = 1003,
    NoStatus = 1005,
    Abnormal = 1006,
    InvalidPayload = 1007,
    PolicyViolation = 1008,
    MessageTooBig = 1009,
    InternalError = 1011,
};

struct Options {
    std::vector<std::string> subprotocols{};
    std::size_t max_message_bytes{1024 * 1024};
    std::size_t max_pending_bytes{4 * 1024 * 1024};
    std::size_t fragment_size{16 * 1024};
    bool auto_pong{true};
};

class Channel;

using TextHandler = std::function<void(Channel&, std::string_view)>;
using BinaryHandler = std::function<void(Channel&, std::string_view)>;
using CloseHandler = std::function<void(Channel&, CloseCode, std::string_view reason)>;
using PingHandler = std::function<void(Channel&, std::string_view payload)>;
using PongHandler = std::function<void(Channel&, std::string_view payload)>;

class Channel {
public:
    struct Impl;

    Channel() = default;
    explicit Channel(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}

    bool valid() const noexcept { return impl_ != nullptr; }
    bool alive() const;

    std::uint64_t id() const;
    std::size_t pending_bytes() const;
    bool writable() const;

    bool send_text(std::string_view data);
    bool send_binary(std::string_view data);
    bool send_raw(std::string_view encoded_frame);
    bool send_text_stream(std::string_view data);
    bool send_binary_stream(std::string_view data);

    bool begin_text();
    bool write_text(std::string_view chunk);
    bool end_text();
    bool begin_binary();
    bool write_binary(std::string_view chunk);
    bool end_binary();

    bool ping(std::string_view payload = {});
    bool pong(std::string_view payload = {});
    void close(CloseCode code = CloseCode::Normal, std::string_view reason = {});

    void on_text(TextHandler fn);
    void on_binary(BinaryHandler fn);
    /**
     * @brief Append a close handler (composable). Handlers run in registration
     *        order when the channel closes. Prefer this over replacing a single
     *        callback so libraries (pulse_easy / pulse_media) can coexist.
     */
    void on_close(CloseHandler fn);
    void on_ping(PingHandler fn);
    void on_pong(PongHandler fn);

    const std::string& protocol() const;
    std::shared_ptr<Impl> impl() const { return impl_; }

private:
    bool begin_fragment_(std::uint8_t opcode);
    bool write_fragment_(std::string_view chunk, bool fin);

    std::shared_ptr<Impl> impl_;
};

Channel upgrade(Request& req, Response& res, Options opts = {});
std::string accept_key(std::string_view client_key);
std::string encode_frame(std::uint8_t opcode, std::string_view payload, bool fin = true);

struct DecodedFrame {
    std::uint8_t opcode{0};
    bool fin{true};
    std::string payload;
    std::size_t bytes_consumed{0};
    bool ok{false};
    bool protocol_error{false};
};
DecodedFrame decode_frame(std::string_view data, std::size_t max_payload);

class Hub {
public:
    void join(std::string room, Channel ch);
    void leave(std::string room, const Channel& ch);
    void leave_all(const Channel& ch);

    void broadcast_text(std::string_view room, std::string_view data);
    void broadcast_binary(std::string_view room, std::string_view data);
    void broadcast_frame(std::string_view room, std::string_view encoded_frame);
    void broadcast_text(std::string_view data);

    std::size_t prune(std::string_view room);
    std::size_t room_size(std::string_view room) const;
    std::vector<Channel> members(std::string_view room) const;

    struct RoomRef {
        Hub* hub;
        std::string name;
        void broadcast_text(std::string_view data) { hub->broadcast_text(name, data); }
        void broadcast_binary(std::string_view data) { hub->broadcast_binary(name, data); }
        void broadcast_frame(std::string_view encoded) { hub->broadcast_frame(name, encoded); }
    };
    RoomRef to(std::string room) { return RoomRef{this, std::move(room)}; }

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, std::vector<Channel>> rooms_;
};

} // namespace socketify::pulse
