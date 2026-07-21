#pragma once
/**
 * @file pulse_impl.h
 * @brief Shared state between pulse::Channel and the server connection.
 */

#include "socketify/pulse.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace socketify::pulse {

struct Channel::Impl {
    static inline std::atomic<std::uint64_t> next_id{1};

    const std::uint64_t id{next_id.fetch_add(1, std::memory_order_relaxed)};
    std::mutex mu;
    std::string pending;
    bool closed{false};
    bool close_requested{false};
    bool close_fired{false};
    std::function<void()> notify;

    Options opts{};
    std::string protocol;

    TextHandler on_text;
    BinaryHandler on_binary;
    CloseHandler on_close;
    PingHandler on_ping;
    PongHandler on_pong;

    std::string inbuf;
    std::string fragment;
    std::uint8_t fragment_opcode{0};

    bool out_frag_active{false};
    bool out_frag_first{true};
    std::uint8_t out_frag_opcode{0};

    std::weak_ptr<Impl> self;

    bool enqueue(std::string_view bytes) {
        std::function<void()> n;
        {
            std::lock_guard<std::mutex> lk(mu);
            if (closed) return false;
            pending.append(bytes);
            n = notify;
        }
        if (n) n();
        return true;
    }
};

bool feed_bytes(const std::shared_ptr<Channel::Impl>& impl, std::string_view data);

} // namespace socketify::pulse
