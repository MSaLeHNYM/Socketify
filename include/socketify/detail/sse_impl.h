#pragma once
/**
 * @file sse_impl.h
 * @brief Shared state between an sse::Session handle and the server
 *        connection that owns the socket. Internal API.
 */

#include "socketify/sse.h"

#include <functional>
#include <mutex>
#include <string>

namespace socketify::sse {

/**
 * @brief Internal shared state for one SSE connection.
 *
 * The user-facing Session appends formatted bytes to @ref pending under
 * @ref mu and invokes @ref notify, which the server wires to "flush this
 * connection on its event-loop thread".
 */
struct Session::Impl {
    std::mutex mu;
    std::string pending;              ///< Bytes waiting to be written.
    bool closed{false};               ///< Connection is gone; drop sends.
    bool close_requested{false};      ///< User asked to close the stream.
    std::function<void()> notify;     ///< Wakes the owning event loop.

    /** @brief Append bytes and wake the loop. @return false when closed. */
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

} // namespace socketify::sse
