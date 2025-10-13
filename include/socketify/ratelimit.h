#pragma once
// socketify/ratelimit.h — IP-based rate limiting middleware

#include "middleware.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace socketify {

namespace rate_limit {

// Options for the rate limiter
struct Options {
    // Max requests per window
    int max_requests{100};

    // Time window
    std::chrono::seconds window{60};

    // Message for rate-limited responses
    std::string message = "Too many requests, please try again later.";

    // Status code for rate-limited responses
    std::uint16_t status_code = 429; // Too Many Requests

    // Optional: Use a custom header for the client IP.
    // If empty, uses the connection's remote address.
    // e.g., "X-Forwarded-For"
    std::string ip_header;
};

// Creates a new rate-limiting middleware.
// This middleware is stateful and should be created once per server.
Middleware Create(Options opts = {});

} // namespace rate_limit

} // namespace socketify