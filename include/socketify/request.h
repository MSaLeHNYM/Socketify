#pragma once
// socketify/request.h — HTTP request representation

#include "socketify/http.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace socketify {

// Key/value type for query params, path params, cookies
using ParamMap  = std::unordered_map<std::string, std::string>;
using CookieMap = std::unordered_map<std::string, std::string>;

class Request {
public:
    Request() = default;

    // --- Basic info ---
    Method method() const noexcept { return method_; }
    std::string_view path() const noexcept { return path_; }
    std::string_view raw_target() const noexcept { return target_; }
    std::string_view http_version() const noexcept { return version_; }

    // --- Headers ---
    const HeaderMap& headers() const noexcept { return headers_; }
    std::string_view header(std::string_view key) const;

    // --- Query / Params / Cookies ---
    const ParamMap& query() const noexcept { return query_; }
    const ParamMap& params() const noexcept { return params_; }
    const CookieMap& cookies() const noexcept { return cookies_; }
    std::string_view cookie(std::string_view key) const;

    // --- Body ---
    std::string_view body_view() const noexcept { return body_; }
    const std::string& body_string() const noexcept { return body_storage_; }
    bool has_body() const noexcept { return !body_.empty() || !body_storage_.empty(); }

    // Internal setters (used by parser / server)
    void set_method(Method m) { method_ = m; }
    void set_path(std::string p) { path_ = std::move(p); }
    void set_target(std::string t) { target_ = std::move(t); }
    void set_version(std::string v) { version_ = std::move(v); }
    HeaderMap& mutable_headers() { return headers_; }
    ParamMap& mutable_query() { return query_; }
    ParamMap& mutable_params() { return params_; }
    CookieMap& mutable_cookies() { return cookies_; }
    void set_body_view(std::string_view view) { body_ = view; }
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
