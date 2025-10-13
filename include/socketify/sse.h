#pragma once
// socketify/sse.h — Server-Sent Events (SSE) helper

#include "response.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace socketify {

// SSE events are UTF-8.
// Spec: https://html.spec.whatwg.org/multipage/server-sent-events.html

// Helper to manage a Server-Sent Events connection.
// An SSE response is just a long-lived HTTP response with a special content type.
class SSE {
public:
    // Create an SSE handler for a given response.
    // The response `res` must not have been ended yet.
    explicit SSE(Response& res);

    // Send a "message" event.
    bool send(std::string_view data);

    // Send a custom-named event.
    bool send_event(std::string_view event_name, std::string_view data);

    // Send a comment (ignored by clients; good for keep-alives).
    bool send_comment(std::string_view comment);

    // Set retry timeout for the client (in milliseconds).
    bool set_retry(unsigned int ms);

    // Explicitly close the connection.
    void close();

    // Check if the connection is still open.
    bool is_open() const noexcept { return !closed_; }

private:
    bool write_chunk_(std::string_view chunk);

    Response& res_;
    std::atomic<bool> closed_{false};
    std::mutex mutex_; // protect writes
};

} // namespace socketify