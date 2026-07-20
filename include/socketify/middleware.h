#pragma once
/**
 * @file middleware.h
 * @brief Handler/Middleware function types and small built-in middleware.
 *
 * A middleware receives the request, the response and a `next` continuation.
 * Call `next()` to pass control down the chain; skip it (and end the
 * response) to short-circuit.
 *
 * @code
 * server.Use([](Request& req, Response& res, Next next) {
 *     if (req.header("X-Api-Key") != "secret") {
 *         res.status(Status::Unauthorized).send("nope\n");
 *         return;                       // short-circuit
 *     }
 *     next();                           // continue to the route handler
 * });
 * @endcode
 */

#include "socketify/http.h"
#include "socketify/request.h"
#include "socketify/response.h"

#include <cstddef>
#include <functional>
#include <string>

namespace socketify {

/** @brief Terminal request handler: fills the response. */
using Handler    = std::function<void(Request&, Response&)>;

/** @brief Continuation passed to middleware; invokes the next stage. */
using Next       = std::function<void()>;

/** @brief Composable request interceptor. */
using Middleware = std::function<void(Request&, Response&, Next)>;

namespace middleware {

/**
 * @brief Assigns every request a unique id.
 *
 * Reuses an incoming X-Request-Id header when present, otherwise generates
 * a random token. The id is echoed in the response X-Request-Id header and
 * available to handlers via `req.header("X-Request-Id")`.
 */
Middleware request_id();

/**
 * @brief Rejects requests whose body exceeds @p max_bytes with 413.
 *
 * The server also enforces ServerOptions::max_body_size globally; use this
 * to apply a stricter limit to a specific route or group.
 */
Middleware body_limit(std::size_t max_bytes);

} // namespace middleware

} // namespace socketify
