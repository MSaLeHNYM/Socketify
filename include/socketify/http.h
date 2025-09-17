#pragma once
// socketify/http.h — foundational HTTP types & helpers

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace socketify {

// ---------- HTTP protocol constants ----------
inline constexpr std::string_view kHTTP11 = "HTTP/1.1";
inline constexpr std::string_view kCRLF   = "\r\n";
inline constexpr std::string_view kSP     = " ";

// ---------- HTTP methods ----------
enum class Method : std::uint8_t {
    GET, POST, PUT, PATCH, DELETE_, HEAD, OPTIONS, CONNECT, TRACE,
    ANY,        // route matches any method (internal use for router)
    UNKNOWN
};

// Convert a Method to its canonical upper-case string (e.g., "GET")
std::string_view to_string(Method m);

// Parse case-insensitively from a string into a Method (unknown→UNKNOWN)
Method method_from_string(std::string_view s);

// ---------- HTTP status codes ----------
enum class Status : std::uint16_t {
    // 1xx
    Continue            = 100,
    SwitchingProtocols  = 101,
    Processing          = 102,

    // 2xx
    OK                  = 200,
    Created             = 201,
    Accepted            = 202,
    NoContent           = 204,
    PartialContent      = 206,

    // 3xx
    MovedPermanently    = 301,
    Found               = 302,
    SeeOther            = 303,
    NotModified         = 304,
    TemporaryRedirect   = 307,
    PermanentRedirect   = 308,

    // 4xx
    BadRequest          = 400,
    Unauthorized        = 401,
    Forbidden           = 403,
    NotFound            = 404,
    MethodNotAllowed    = 405,
    RequestTimeout      = 408,
    Conflict            = 409,
    Gone                = 410,
    PayloadTooLarge     = 413,
    URITooLong          = 414,
    UnsupportedMediaType= 415,
    TooManyRequests     = 429,

    // 5xx
    InternalServerError = 500,
    NotImplemented      = 501,
    BadGateway          = 502,
    ServiceUnavailable  = 503,
    GatewayTimeout      = 504
};

// Reason phrase for a status (e.g., 404 -> "Not Found")
std::string_view reason(Status s);

// ---------- Case-insensitive ASCII hashing/equality for header maps ----------
struct ci_hash {
    using is_transparent = void; // enables heterogenous lookup
    std::size_t operator()(std::string_view s) const noexcept;
};
struct ci_equal {
    using is_transparent = void;
    bool operator()(std::string_view a, std::string_view b) const noexcept;
};

// Common header container (ASCII case-insensitive keys)
using HeaderMap = std::unordered_map<std::string, std::string, ci_hash, ci_equal>;

// ---------- MIME helpers ----------
// Returns MIME type for an extension (".html" or "html"), or "application/octet-stream" if unknown.
std::string_view mime_from_ext(std::string_view ext);

// Returns MIME type inferred from a filesystem path by its extension.
std::string_view content_type_for_path(std::string_view path);

// Default MIME when unknown
inline constexpr std::string_view kDefaultMime = "application/octet-stream";

// A few canonical header names (optional convenience)
inline constexpr std::string_view H_ContentType   = "Content-Type";
inline constexpr std::string_view H_ContentLength = "Content-Length";
inline constexpr std::string_view H_Connection    = "Connection";
inline constexpr std::string_view H_Date          = "Date";
inline constexpr std::string_view H_Server        = "Server";
inline constexpr std::string_view H_Location      = "Location";
inline constexpr std::string_view H_SetCookie     = "Set-Cookie";
inline constexpr std::string_view H_Cookie        = "Cookie";
inline constexpr std::string_view H_AcceptRanges  = "Accept-Ranges";
inline constexpr std::string_view H_LastModified  = "Last-Modified";
inline constexpr std::string_view H_ETag          = "ETag";

} // namespace socketify
