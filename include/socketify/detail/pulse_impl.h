#pragma once
/**
 * @file pulse_impl.h
 * @brief Shared state between pulse::Channel and the server connection.
 */

#include "socketify/pulse.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace socketify::pulse {

struct Channel::Impl {
    std::mutex mu;
    std::string pending; ///< Outbound framed bytes.
    bool closed{false};
    bool close_requested{false};
    bool close_fired{false};
    std::function<void()> notify;

    Options opts{};
    std::string protocol;

    TextHandler on_text;
    BinaryHandler on_binary;
    CloseHandler on_close;

    // Inbound reassembly
    std::string inbuf;
    std::string fragment;
    std::uint8_t fragment_opcode{0};

    // Weak self handle for callbacks (set after Channel constructed)
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

/** @brief Feed raw socket bytes; invoke message callbacks. Returns false on fatal protocol error. */
bool feed_bytes(const std::shared_ptr<Channel::Impl>& impl, std::string_view data);

} // namespace socketify::pulse
