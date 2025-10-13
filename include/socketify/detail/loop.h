#pragma once
// socketify/detail/loop.h — Event loop abstraction

#include "socket.h" // for NativeSocket
#include <functional>
#include <memory>

namespace socketify::detail {

// Event types for the event loop
enum class EventType {
    Read,
    Write,
};

// Callback for I/O events
using EventCallback = std::function<void(NativeSocket fd, EventType type)>;

// An abstraction for an I/O event loop (e.g., epoll, kqueue, select).
class EventLoop {
public:
    virtual ~EventLoop() = default;

    // Create a platform-specific event loop.
    static std::unique_ptr<EventLoop> create();

    // Add a socket to the loop to monitor for events.
    virtual bool add(NativeSocket fd, void* user_data) = 0;

    // Modify the events being monitored for a socket.
    virtual bool modify(NativeSocket fd, void* user_data, bool read, bool write) = 0;

    // Remove a socket from the loop.
    virtual bool remove(NativeSocket fd) = 0;

    // Wait for events and dispatch them.
    // `timeout_ms`: -1 to wait forever.
    virtual void poll(EventCallback cb, int timeout_ms) = 0;
};

} // namespace socketify::detail