#pragma once
/**
 * @file request.h
 * @brief HTTP request representation passed to handlers and middleware.
 */

#include "socketify/http.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace socketify {

/** @brief Key/value map for query parameters, path parameters and cookies. */
using ParamMap  = std::unordered_map<std::string, std::string>;
/** @brief Key/value map for request cookies. */
using CookieMap = std::unordered_map<std::string, std::string>;

/**
 * @brief An incoming HTTP request.
 *
 * A Request is created by the server for every parsed HTTP message and is
 * valid only for the duration of the handler call. All views point into
 * request-owned storage.
 *
 * @code
 * server.AddRoute(Method::GET, "/users/:id", [](Request& req, Response& res) {
 *     std::string id = req.params().at("id");
 *     std::string_view page = req.query_value("page");   // "" when absent
 *     res.json({{"id", id}, {"page", page}});
 * });
 * @endcode
 */
class Request {
public:
    Request() = default;

    // ------------------------------------------------------------------
    // Basic info
    // ------------------------------------------------------------------

    /** @brief HTTP method (GET, POST, ...). */
    Method method() const noexcept { return method_; }

    /** @brief Decoded path without the query string, e.g. "/api/users". */
    std::string_view path() const noexcept { return path_; }

    /** @brief Raw request-target as sent by the client, e.g. "/api/users?id=42". */
    std::string_view raw_target() const noexcept { return target_; }

    /** @brief HTTP version string, e.g. "HTTP/1.1". */
    std::string_view http_version() const noexcept { return version_; }

    /** @brief Client IP address in text form ("203.0.113.7" or "::1"). */
    std::string_view remote_ip() const noexcept { return remote_ip_; }

    // ------------------------------------------------------------------
    // Headers
    // ------------------------------------------------------------------

    /** @brief All request headers (keys are case-insensitive). */
    const HeaderMap& headers() const noexcept { return headers_; }

    /**
     * @brief Look up a single header value.
     * @return The value, or an empty view when the header is absent.
     */
    std::string_view header(std::string_view key) const;

    /** @brief Content-Type header value ("" when absent). */
    std::string_view content_type() const { return header(H_ContentType); }

    // ------------------------------------------------------------------
    // Query / path params / cookies
    // ------------------------------------------------------------------

    /** @brief Decoded query-string parameters ("?a=1&b=2"). */
    const ParamMap& query() const noexcept { return query_; }

    /** @brief Value of a single query parameter ("" when absent). */
    std::string_view query_value(std::string_view key) const;

    /** @brief Path parameters bound by the router ("/users/:id"). */
    const ParamMap& params() const noexcept { return params_; }

    /** @brief Parsed cookies from the Cookie header. */
    const CookieMap& cookies() const noexcept { return cookies_; }

    /** @brief Value of a single cookie ("" when absent). */
    std::string_view cookie(std::string_view key) const;

    // ------------------------------------------------------------------
    // Body
    // ------------------------------------------------------------------

    /** @brief Request body as a view (valid for the handler's lifetime). */
    std::string_view body_view() const noexcept { return body_; }

    /** @brief Request body as an owned string reference. */
    const std::string& body_string() const noexcept { return body_storage_; }

    /** @brief True when a non-empty body was received. */
    bool has_body() const noexcept { return !body_.empty() || !body_storage_.empty(); }

    // ------------------------------------------------------------------
    // Per-request locals (used by middleware such as sessions)
    // ------------------------------------------------------------------

    /**
     * @brief Attach an arbitrary object to this request under @p key.
     *
     * Middleware uses this to pass data to downstream handlers, e.g. the
     * sessions middleware stores the active Session here.
     */
    void set_local(std::string key, std::shared_ptr<void> value) {
        locals_[std::move(key)] = std::move(value);
    }

    /**
     * @brief Retrieve an object previously stored with set_local().
     * @tparam T The stored type.
     * @return Shared pointer to the object, or nullptr when absent.
     */
    template <typename T>
    std::shared_ptr<T> local(const std::string& key) const {
        auto it = locals_.find(key);
        if (it == locals_.end()) return nullptr;
        return std::static_pointer_cast<T>(it->second);
    }

    // ------------------------------------------------------------------
    // Internal setters (used by the server / parser; not for handlers)
    // ------------------------------------------------------------------

    /** @brief Internal: set the parsed method. */
    void set_method(Method m) { method_ = m; }
    /** @brief Internal: set the decoded path. */
    void set_path(std::string p) { path_ = std::move(p); }
    /** @brief Internal: set the raw request-target. */
    void set_target(std::string t) { target_ = std::move(t); }
    /** @brief Internal: set the HTTP version string. */
    void set_version(std::string v) { version_ = std::move(v); }
    /** @brief Internal: set the client address. */
    void set_remote_ip(std::string ip) { remote_ip_ = std::move(ip); }
    /** @brief Internal: mutable access to headers. */
    HeaderMap& mutable_headers() { return headers_; }
    /** @brief Internal: mutable access to query parameters. */
    ParamMap& mutable_query() { return query_; }
    /** @brief Internal: mutable access to path parameters. */
    ParamMap& mutable_params() { return params_; }
    /** @brief Internal: mutable access to cookies. */
    CookieMap& mutable_cookies() { return cookies_; }
    /** @brief Internal: set the body as a non-owning view. */
    void set_body_view(std::string_view view) { body_ = view; }
    /** @brief Internal: set the body, transferring ownership. */
    void set_body_storage(std::string b) { body_storage_ = std::move(b); body_ = body_storage_; }

private:
    Method method_{Method::UNKNOWN};
    std::string path_;          ///< decoded path (/api/v2/user)
    std::string target_;        ///< raw request-target (/api/v2/user?id=42)
    std::string version_;       ///< HTTP version (HTTP/1.1)
    std::string remote_ip_;     ///< client address

    HeaderMap headers_;
    ParamMap query_;
    ParamMap params_;
    CookieMap cookies_;

    std::string body_storage_;  ///< owns body data if we copied it
    std::string_view body_;     ///< view into buffer or body_storage

    std::unordered_map<std::string, std::shared_ptr<void>> locals_;
};

} // namespace socketify
