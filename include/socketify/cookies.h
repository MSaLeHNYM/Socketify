#pragma once
/**
 * @file cookies.h
 * @brief Cookie parsing (Cookie header) and building (Set-Cookie header).
 *
 * @code
 * res.set_cookie(Cookie("session", token)
 *                    .path("/")
 *                    .http_only(true)
 *                    .secure(true)
 *                    .same_site(SameSite::Lax)
 *                    .max_age(3600));
 * @endcode
 */

#include "socketify/http.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace socketify {

/** @brief SameSite cookie attribute values. */
enum class SameSite {
    None,   ///< Sent on cross-site requests (requires Secure).
    Lax,    ///< Sent on top-level navigations (browser default).
    Strict  ///< Never sent cross-site.
};

/**
 * @brief Builder for a Set-Cookie header value.
 *
 * All setters return *this so attributes can be chained fluently.
 */
class Cookie {
public:
    /** @brief Create a cookie with @p name and @p value. */
    Cookie(std::string name, std::string value)
        : name_(std::move(name)), value_(std::move(value)) {}

    /** @brief Path attribute (default none). */
    Cookie& path(std::string p) { path_ = std::move(p); return *this; }
    /** @brief Domain attribute. */
    Cookie& domain(std::string d) { domain_ = std::move(d); return *this; }
    /** @brief Max-Age in seconds; negative values expire the cookie. */
    Cookie& max_age(std::int64_t seconds) { max_age_ = seconds; return *this; }
    /** @brief Expires attribute as unix time. */
    Cookie& expires(std::int64_t unix_seconds) { expires_ = unix_seconds; return *this; }
    /** @brief Secure attribute (HTTPS only). */
    Cookie& secure(bool on = true) { secure_ = on; return *this; }
    /** @brief HttpOnly attribute (hidden from JavaScript). */
    Cookie& http_only(bool on = true) { http_only_ = on; return *this; }
    /** @brief SameSite attribute. */
    Cookie& same_site(SameSite s) { same_site_ = s; return *this; }

    /** @brief Cookie name. */
    const std::string& name() const noexcept { return name_; }
    /** @brief Cookie value. */
    const std::string& value() const noexcept { return value_; }

    /** @brief Serialize to a Set-Cookie header value. */
    std::string to_string() const;

private:
    std::string name_;
    std::string value_;
    std::string path_;
    std::string domain_;
    std::optional<std::int64_t> max_age_;
    std::optional<std::int64_t> expires_;
    bool secure_{false};
    bool http_only_{false};
    std::optional<SameSite> same_site_;
};

namespace cookies {

/**
 * @brief Parse a Cookie request header ("a=1; b=2") into @p out.
 *
 * Malformed pairs are skipped; values are used verbatim (no decoding).
 */
void parse_cookie_header(std::string_view header, std::unordered_map<std::string, std::string>& out);

/** @brief Build a Set-Cookie value that deletes @p name in the browser. */
std::string expired(std::string_view name, std::string_view path = "/");

} // namespace cookies

} // namespace socketify
