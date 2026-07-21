#pragma once
/**
 * @file http_client.h
 * @brief Minimal synchronous outbound HTTP/HTTPS client.
 *
 * A small blocking client for talking to other services from a handler or a
 * background thread. Supports GET/POST/etc., request/response headers,
 * Content-Length and chunked bodies, and (when built with TLS) https.
 *
 * @code
 * using namespace socketify;
 * auto res = http_client::get("https://api.example.com/ping");
 * if (res.ok()) {
 *     if (auto j = res.json()) { ... }
 * } else {
 *     log(res.error);   // network/parse failure => status == 0
 * }
 *
 * auto created = http_client::post("http://localhost:8080/api/items",
 *                                  R"({"name":"x"})",
 *                                  {{"Content-Type", "application/json"}});
 * @endcode
 */

#include "socketify/http.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace socketify::http_client {

/** @brief An outbound request. */
struct Request {
    Method method{Method::GET};
    std::string url;                 ///< http(s)://host[:port]/path?query
    HeaderMap headers;               ///< extra request headers
    std::string body;                ///< request body (for POST/PUT/PATCH)
    long timeout_ms{15000};          ///< connect/read timeout in milliseconds
};

/** @brief A response (or a transport error when status == 0). */
struct Response {
    int status{0};                   ///< HTTP status; 0 means the request failed
    HeaderMap headers;               ///< response headers
    std::string body;                ///< decoded response body
    std::string error;               ///< non-empty when status == 0

    /** @brief True for 2xx responses. */
    bool ok() const noexcept { return status >= 200 && status < 300; }

    /** @brief Parse the body as JSON (nullopt when empty/invalid). */
    std::optional<nlohmann::json> json() const;
};

/** @brief Perform an arbitrary request. */
Response request(const Request& req);

/** @brief GET @p url with optional extra headers. */
Response get(const std::string& url, HeaderMap headers = {});

/**
 * @brief POST @p body to @p url.
 * @note Sets Content-Type: application/json when the caller did not provide one.
 */
Response post(const std::string& url, std::string body, HeaderMap headers = {});

} // namespace socketify::http_client
