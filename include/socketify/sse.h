#pragma once
/**
 * @file sse.h
 * @brief Server-Sent Events: push a text/event-stream to the browser.
 *
 * Upgrade a request inside a normal handler; the returned Session is a
 * thread-safe handle that outlives the handler and can be used from any
 * thread (e.g. a broadcast loop):
 *
 * @code
 * server.Get("/events", [&hub](Request& req, Response& res) {
 *     sse::Session s = sse::upgrade(req, res);
 *     hub.add(s);                      // keep the handle somewhere
 *     s.send_event("welcome", "hello");
 * });
 *
 * // elsewhere, possibly on another thread:
 * hub.broadcast("tick", std::to_string(now));
 * @endcode
 */

#include "socketify/request.h"
#include "socketify/response.h"

#include <memory>
#include <string>
#include <string_view>

namespace socketify::sse {

/**
 * @brief Thread-safe handle to an open event-stream connection.
 *
 * Copies share the same underlying connection. All methods are safe to
 * call from any thread; sends after the client disconnected are dropped
 * and return false.
 */
class Session {
public:
    struct Impl; ///< Internal shared state (server-managed).

    Session() = default;
    /** @brief Internal: wrap the shared state. */
    explicit Session(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}

    /**
     * @brief Send a data-only event ("data: ...\\n\\n").
     * @return false when the connection is closed.
     */
    bool send(std::string_view data);

    /**
     * @brief Send a named event with optional id.
     * @param event Event name ("message" semantics when empty).
     * @param data  Payload; embedded newlines become multiple data: lines.
     * @param id    Optional event id for client-side resume.
     */
    bool send_event(std::string_view event, std::string_view data, std::string_view id = "");

    /** @brief Send a comment line (": ..."), useful as keep-alive ping. */
    bool comment(std::string_view text);

    /** @brief Close the stream (the client sees a clean end). */
    void close();

    /** @brief True while the client is still connected. */
    bool alive() const;

    /** @brief True when this handle is bound to a connection. */
    bool valid() const noexcept { return impl_ != nullptr; }

private:
    std::shared_ptr<Impl> impl_;
};

/**
 * @brief Convert the current request/response into an SSE stream.
 *
 * Sets the text/event-stream headers and marks the response as streaming;
 * the server keeps the connection open after the handler returns.
 *
 * @return A Session handle for pushing events (also from other threads).
 */
Session upgrade(Request& req, Response& res);

} // namespace socketify::sse
