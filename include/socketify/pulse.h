#pragma once
/**
 * @file pulse.h
 * @brief Pulse — Socketify bidirectional realtime channels (RFC 6455 under the hood).
 *
 * Keep the connection pulsing. Browser `WebSocket`, bots, and messengers all
 * speak the same wire protocol (`ws://` / `wss://`).
 *
 * @code
 * pulse::Hub hub;
 * server.Get("/chat", [&](Request& req, Response& res) {
 *     auto ch = pulse::upgrade(req, res);
 *     hub.join("lobby", ch);
 *     ch.on_text([&](pulse::Channel& c, std::string_view msg) {
 *         hub.to("lobby").broadcast_text(msg);
 *     });
 *     ch.send_text(R"({"type":"welcome"})");
 * });
 * @endcode
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
#include <unordered_set>
#include <vector>

namespace socketify::pulse {

/** @brief WebSocket close status codes (subset). */
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
    /** @brief Preferred subprotocols; first client match is selected. */
    std::vector<std::string> subprotocols{};
    /** @brief Max assembled message size (bytes). */
    std::size_t max_message_bytes{1024 * 1024};
    /** @brief Automatically reply to Ping with Pong (default on). */
    bool auto_pong{true};
};

class Channel;

using TextHandler = std::function<void(Channel&, std::string_view)>;
using BinaryHandler = std::function<void(Channel&, std::string_view)>;
using CloseHandler = std::function<void(Channel&, CloseCode, std::string_view reason)>;

/**
 * @brief Thread-safe handle to one Pulse (WebSocket) connection.
 *
 * Copies share state. Callbacks run on the connection's worker thread.
 * `send_*` is safe from any thread.
 */
class Channel {
public:
    struct Impl;

    Channel() = default;
    explicit Channel(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}

    bool valid() const noexcept { return impl_ != nullptr; }
    bool alive() const;

    bool send_text(std::string_view data);
    bool send_binary(std::string_view data);
    bool ping(std::string_view payload = {});
    bool pong(std::string_view payload = {});
    void close(CloseCode code = CloseCode::Normal, std::string_view reason = {});

    void on_text(TextHandler fn);
    void on_binary(BinaryHandler fn);
    void on_close(CloseHandler fn);

    /** @brief Negotiated subprotocol (empty if none). */
    const std::string& protocol() const;

    std::shared_ptr<Impl> impl() const { return impl_; }

private:
    std::shared_ptr<Impl> impl_;
};

/**
 * @brief Upgrade an HTTP request to a Pulse channel (101 Switching Protocols).
 * @throws nothing — on failure sets 4xx on @p res and returns an invalid Channel.
 */
Channel upgrade(Request& req, Response& res, Options opts = {});

/** @brief Compute Sec-WebSocket-Accept for @p client_key (exposed for tests). */
std::string accept_key(std::string_view client_key);

/** @brief Encode a server→client frame (unmasked). Exposed for tests. */
std::string encode_frame(std::uint8_t opcode, std::string_view payload, bool fin = true);

/** @brief Decode one client→server frame from @p data; returns bytes consumed (0 = need more). */
struct DecodedFrame {
    std::uint8_t opcode{0};
    bool fin{true};
    std::string payload;
    std::size_t bytes_consumed{0};
    bool ok{false};
    bool protocol_error{false};
};
DecodedFrame decode_frame(std::string_view data, std::size_t max_payload);

/**
 * @brief Room / broadcast helper for messengers and chatbots.
 */
class Hub {
public:
    void join(std::string room, Channel ch);
    void leave(std::string room, const Channel& ch);
    void leave_all(const Channel& ch);

    /** @brief Send text to every member of @p room (skips dead channels). */
    void broadcast_text(std::string_view room, std::string_view data);
    void broadcast_binary(std::string_view room, std::string_view data);

    /** @brief Broadcast to every joined channel in any room. */
    void broadcast_text(std::string_view data);

    std::size_t room_size(std::string_view room) const;

    /** @brief Fluent room target: hub.to("lobby").broadcast_text(...) */
    struct RoomRef {
        Hub* hub;
        std::string name;
        void broadcast_text(std::string_view data) { hub->broadcast_text(name, data); }
        void broadcast_binary(std::string_view data) { hub->broadcast_binary(name, data); }
    };
    RoomRef to(std::string room) { return RoomRef{this, std::move(room)}; }

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, std::vector<Channel>> rooms_;
};

} // namespace socketify::pulse
