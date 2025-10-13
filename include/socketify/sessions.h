#pragma once
// socketify/sessions.h — Basic session management

#include "middleware.h"

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace socketify {

// A simple key-value store for session data.
using SessionData = std::unordered_map<std::string, std::string>;

// Represents a single session.
class Session {
public:
    Session() = default;

    // Get a value from the session. Returns empty view if not found.
    std::string_view get(std::string_view key) const;

    // Set a value in the session.
    void set(std::string key, std::string value);

    // Remove a value from the session.
    void unset(std::string_view key);

    // Check if the session is empty.
    bool empty() const { return data_.empty(); }

    // Mark the session for destruction at the end of the request.
    void destroy();
    bool is_destroyed() const { return destroyed_; }

private:
    friend class SessionMiddleware;
    SessionData data_;
    bool destroyed_{false};
};


namespace sessions {

// Options for session middleware
struct Options {
    // Cookie name for the session ID.
    std::string cookie_name = "sid";

    // Secret for signing the session ID cookie.
    // **MUST** be a long, random, and secret string.
    std::string secret;

    // Cookie attributes
    std::string cookie_path = "/";
    std::string cookie_domain = "";
    bool http_only = true;
    bool secure = false; // true for HTTPS only
    std::chrono::seconds max_age{86400}; // 1 day
};

// Creates a session management middleware.
// This should be one of the first middlewares in the chain.
Middleware Create(Options opts);

// How to access the session from a request handler:
//
// auto session = req.context<Session>("session");
// if (session) {
//   session->set("user_id", "123");
// }
//
// The context requires a mechanism to store arbitrary data with a request.
// This will be added to the Request object later. For now, we assume it exists.

} // namespace sessions

} // namespace socketify