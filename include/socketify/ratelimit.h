#pragma once
/**
 * @file ratelimit.h
 * @brief Token-bucket rate limiting middleware.
 *
 * Each key (client IP by default) gets a bucket holding up to
 * Options::capacity tokens, refilled at Options::refill_per_second. Every
 * request costs one token; empty buckets get 429 with a Retry-After header.
 *
 * @code
 * ratelimit::Options rl;
 * rl.capacity = 20;              // burst
 * rl.refill_per_second = 5.0;    // sustained rate
 * server.Use(ratelimit::middleware(rl));
 * @endcode
 */

#include "socketify/middleware.h"

#include <chrono>
#include <cstddef>
#include <functional>
#include <string>

namespace socketify::ratelimit {

/** @brief Rate-limiter configuration. */
struct Options {
    /** @brief Max tokens (burst size). */
    double capacity{60.0};
    /** @brief Tokens added per second (sustained request rate). */
    double refill_per_second{1.0};
    /**
     * @brief Key extractor; defaults to the client IP. Return an empty
     *        string to exempt the request from limiting.
     */
    std::function<std::string(const Request&)> key_fn{};
    /** @brief Emit RateLimit-Limit/Remaining/Reset headers (default on). */
    bool standard_headers{true};
    /** @brief Response body sent with 429 replies. */
    std::string message{"Too Many Requests\n"};
};

/**
 * @brief Create the middleware. Each middleware instance owns an
 *        independent in-memory bucket store (thread-safe, self-pruning).
 */
Middleware middleware(Options opts = {});

} // namespace socketify::ratelimit
