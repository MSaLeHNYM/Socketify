#pragma once
// socketify/request.h — HTTP request representation

#include "socketify/http.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace socketify {

// Key/value type for query params, path params, cookies
/**
 * @brief A map for URL query parameters.
 */
using ParamMap  = std::unordered_map<std::string, std::string>;
/**
 * @brief A map for cookies.
 */
using CookieMap = std::unordered_map<std::string, std::string>;

/**
 * @brief Represents an incoming HTTP request.
 */
class Request {
public:
    Request() = default;

    // --- Basic info ---
    /**
     * @brief Gets the HTTP method of the request.
     * @return The HTTP method.
     */
    Method method() const noexcept { return method_; }
    /**
     * @brief Gets the path of the request.
     * @return The path.
     */
    std::string_view path() const noexcept { return path_; }
    /**
     * @brief Gets the raw request target.
     * @return The raw request target.
     */
    std::string_view raw_target() const noexcept { return target_; }
    /**
     * @brief Gets the HTTP version of the request.
     * @return The HTTP version.
     */
    std::string_view http_version() const noexcept { return version_; }

    // --- Headers ---
    /**
     * @brief Gets the headers of the request.
     * @return A const reference to the header map.
     */
    const HeaderMap& headers() const noexcept { return headers_; }
    /**
     * @brief Gets a specific header value.
     * @param key The header key.
     * @return The header value.
     */
    std::string_view header(std::string_view key) const;

    // --- Query / Params / Cookies ---
    /**
     * @brief Gets the query parameters of the request.
     * @return A const reference to the query parameter map.
     */
    const ParamMap& query() const noexcept { return query_; }
    /**
     * @brief Gets the path parameters of the request.
     * @return A const reference to the path parameter map.
     */
    const ParamMap& params() const noexcept { return params_; }
    /**
     * @brief Gets the cookies of the request.
     * @return A const reference to the cookie map.
     */
    const CookieMap& cookies() const noexcept { return cookies_; }
    /**
     * @brief Gets a specific cookie value.
     * @param key The cookie key.
     * @return The cookie value.
     */
    std::string_view cookie(std::string_view key) const;

    // --- Body ---
    /**
     * @brief Gets a view of the request body.
     * @return A string view of the body.
     */
    std::string_view body_view() const noexcept { return body_; }
    /**
     * @brief Gets the request body as a string.
     * @return The body as a string.
     */
    const std::string& body_string() const noexcept { return body_storage_; }
    /**
     * @brief Checks if the request has a body.
     * @return true if the request has a body, false otherwise.
     */
    bool has_body() const noexcept { return !body_.empty() || !body_storage_.empty(); }

    // Internal setters (used by parser / server)
    /**
     * @brief Sets the HTTP method.
     * @param m The HTTP method.
     */
    void set_method(Method m) { method_ = m; }
    /**
     * @brief Sets the path.
     * @param p The path.
     */
    void set_path(std::string p) { path_ = std::move(p); }
    /**
     * @brief Sets the raw request target.
     * @param t The raw request target.
     */
    void set_target(std::string t) { target_ = std::move(t); }
    /**
     * @brief Sets the HTTP version.
     * @param v The HTTP version.
     */
    void set_version(std::string v) { version_ = std::move(v); }
    /**
     * @brief Gets a mutable reference to the headers.
     * @return A mutable reference to the headers.
     */
    HeaderMap& mutable_headers() { return headers_; }
    /**
     * @brief Gets a mutable reference to the query parameters.
     * @return A mutable reference to the query parameters.
     */
    ParamMap& mutable_query() { return query_; }
    /**
     * @brief Gets a mutable reference to the path parameters.
     * @return A mutable reference to the path parameters.
     */
    ParamMap& mutable_params() { return params_; }
    /**
     * @brief Gets a mutable reference to the cookies.
     * @return A mutable reference to the cookies.
     */
    CookieMap& mutable_cookies() { return cookies_; }
    /**
     * @brief Sets the body from a string view.
     * @param view The string view of the body.
     */
    void set_body_view(std::string_view view) { body_ = view; }
    /**
     * @brief Sets the body from a string.
     * @param b The body string.
     */
    void set_body_storage(std::string b) { body_storage_ = std::move(b); body_ = body_storage_; }

private:
    Method method_{Method::UNKNOWN};
    std::string path_;          // decoded path (/api/v2/user)
    std::string target_;        // raw request-target (/api/v2/user?id=42)
    std::string version_;       // HTTP version (HTTP/1.1)

    HeaderMap headers_;
    ParamMap query_;
    ParamMap params_;
    CookieMap cookies_;

    std::string body_storage_;  // owns body data if we copied it
    std::string_view body_;     // view into buffer or body_storage
};

} // namespace socketify