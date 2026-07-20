#pragma once
/**
 * @file utils.h
 * @brief Internal shared helpers: ASCII case utilities, URL decoding,
 *        query-string parsing, splitting, trimming and random tokens.
 *
 * Everything in socketify::detail is internal API and may change between
 * minor versions. Do not use from application code.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace socketify::detail {

/** @brief Lowercase a single ASCII character (locale independent). */
inline constexpr char ascii_lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

/** @brief Case-insensitive ASCII comparison of two string views. */
inline bool iequal_ascii(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (ascii_lower(a[i]) != ascii_lower(b[i])) return false;
    return true;
}

/** @brief Return a lowercase copy of @p s. */
inline std::string to_lower_copy(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = ascii_lower(c);
    return out;
}

/** @brief True when @p s starts with @p pfx (case sensitive). */
inline bool starts_with(std::string_view s, std::string_view pfx) noexcept {
    return s.size() >= pfx.size() && s.compare(0, pfx.size(), pfx) == 0;
}

/** @brief True when @p s starts with @p pfx, ASCII case-insensitive. */
inline bool istarts_with(std::string_view s, std::string_view pfx) noexcept {
    if (s.size() < pfx.size()) return false;
    return iequal_ascii(s.substr(0, pfx.size()), pfx);
}

/** @brief Strip leading/trailing spaces and horizontal tabs. */
inline std::string_view trim_view(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

/**
 * @brief Split @p s on @p sep, skipping empty pieces.
 * @return Vector of views into @p s (caller must keep @p s alive).
 */
inline std::vector<std::string_view> split_view(std::string_view s, char sep) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (start <= s.size()) {
        std::size_t pos = s.find(sep, start);
        if (pos == std::string_view::npos) {
            if (start < s.size()) out.push_back(s.substr(start));
            break;
        }
        if (pos > start) out.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

/** @brief Value of a hex digit, or -1 when @p c is not a hex digit. */
inline int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/**
 * @brief Percent-decode @p in.
 * @param in            Raw (encoded) input.
 * @param out           Receives the decoded string on success.
 * @param plus_as_space When true, '+' decodes to ' ' (form encoding rules).
 * @return false when the input contains a malformed %XX escape.
 */
inline bool url_decode(std::string_view in, std::string& out, bool plus_as_space = false) {
    out.clear();
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (c == '%') {
            if (i + 2 >= in.size()) return false;
            int hi = hex_value(in[i + 1]);
            int lo = hex_value(in[i + 2]);
            if (hi < 0 || lo < 0) return false;
            out.push_back(static_cast<char>((hi << 4) | lo));
            i += 2;
        } else if (c == '+' && plus_as_space) {
            out.push_back(' ');
        } else {
            out.push_back(c);
        }
    }
    return true;
}

/**
 * @brief Parse an application/x-www-form-urlencoded string ("a=1&b=2")
 *        into @p out. Malformed escapes keep the raw text.
 */
template <typename Map>
inline void parse_query_string(std::string_view qs, Map& out) {
    for (auto pair : split_view(qs, '&')) {
        if (pair.empty()) continue;
        std::string_view key = pair;
        std::string_view val{};
        if (auto eq = pair.find('='); eq != std::string_view::npos) {
            key = pair.substr(0, eq);
            val = pair.substr(eq + 1);
        }
        std::string k, v;
        if (!url_decode(key, k, true)) k.assign(key);
        if (!url_decode(val, v, true)) v.assign(val);
        if (!k.empty()) out[std::move(k)] = std::move(v);
    }
}

/** @brief Cryptographically-random token of @p bytes bytes, hex encoded. */
std::string random_token(std::size_t bytes = 16);

/** @brief SHA-256 digest (32 bytes) of @p data. Self-contained implementation. */
std::array<std::uint8_t, 32> sha256(std::string_view data);

/** @brief HMAC-SHA256 of @p data keyed with @p key. */
std::array<std::uint8_t, 32> hmac_sha256(std::string_view key, std::string_view data);

/** @brief Constant-time equality check for fixed-size digests / tokens. */
bool constant_time_equal(std::string_view a, std::string_view b) noexcept;

/** @brief URL-safe base64 encode (no padding). */
std::string base64url_encode(const unsigned char* data, std::size_t len);

/** @brief URL-safe base64 decode; returns std::nullopt on malformed input. */
std::optional<std::string> base64url_decode(std::string_view in);

/** @brief Hex-encode a binary buffer (lowercase). */
std::string hex_encode(const unsigned char* data, std::size_t len);

/** @brief RFC 7231 IMF-fixdate for the current time, e.g. "Tue, 15 Nov 1994 08:12:31 GMT". */
std::string http_date_now();

/** @brief RFC 7231 IMF-fixdate for @p t. */
std::string http_date(std::int64_t unix_seconds);

/** @brief Parse an IMF-fixdate; returns std::nullopt when malformed. */
std::optional<std::int64_t> parse_http_date(std::string_view s);

} // namespace socketify::detail
