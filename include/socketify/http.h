#pragma once
/**
 * @file http.h
 * @brief Core HTTP enums, types, constants and helpers.
 *
 * Defines the Method and Status enums, the case-insensitive HeaderMap and
 * MIME-type helpers shared by the whole framework.
 */

#include <string>
#include <string_view>
#include <unordered_map>
#include <cstddef>
#include <cstdint>

namespace socketify {

// ------------------------------
// Case-insensitive string utils
// ------------------------------

/** @brief Case-insensitive FNV-1a hash for header names. */
struct ci_hash {
    std::size_t operator()(std::string_view s) const noexcept;
};

/** @brief Case-insensitive equality for header names. */
struct ci_equal {
    bool operator()(std::string_view a, std::string_view b) const noexcept;
};

/**
 * @brief Header map with case-insensitive keys.
 *
 * "Content-Type", "content-type" and "CONTENT-TYPE" address the same entry.
 */
using HeaderMap = std::unordered_map<std::string, std::string, ci_hash, ci_equal>;

// ------------------------------
// Methods
// ------------------------------

/** @brief HTTP request methods. */
enum class Method {
    UNKNOWN = 0,
    GET, POST, PUT, PATCH,
    DELETE_,        ///< maps to "DELETE" ("DELETE" is a macro-unsafe identifier)
    OPTIONS, HEAD,
    CONNECT, TRACE,
    ANY             ///< router wildcard: matches every method
};

/** @brief Canonical name of @p m, e.g. "GET". */
std::string_view to_string(Method m);

/** @brief Parse a method name (case-insensitive); UNKNOWN when unrecognized. */
Method method_from_string(std::string_view s);

// ------------------------------
// Status codes
// ------------------------------

/** @brief HTTP response status codes (common subset). */
enum class Status : unsigned short {
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
    NotAcceptable       = 406,
    RequestTimeout      = 408,
    Conflict            = 409,
    Gone                = 410,
    LengthRequired      = 411,
    PayloadTooLarge     = 413,
    URITooLong          = 414,
    UnsupportedMediaType= 415,
    RangeNotSatisfiable = 416,
    UnprocessableEntity = 422,
    TooManyRequests     = 429,
    RequestHeaderFieldsTooLarge = 431,

    // 5xx
    InternalServerError = 500,
    NotImplemented      = 501,
    BadGateway          = 502,
    ServiceUnavailable  = 503,
    GatewayTimeout      = 504,
    HTTPVersionNotSupported = 505
};

/** @brief Reason phrase for @p s, e.g. "Not Found". */
std::string_view reason(Status s);

// ------------------------------
// Common header names (constants)
// ------------------------------
inline constexpr std::string_view H_ContentLength   = "Content-Length";
inline constexpr std::string_view H_ContentType     = "Content-Type";
inline constexpr std::string_view H_Connection      = "Connection";
inline constexpr std::string_view H_SetCookie       = "Set-Cookie";
inline constexpr std::string_view H_Cookie          = "Cookie";
inline constexpr std::string_view H_Location        = "Location";
inline constexpr std::string_view H_TransferEncoding= "Transfer-Encoding";
inline constexpr std::string_view H_AcceptEncoding  = "Accept-Encoding";
inline constexpr std::string_view H_ContentEncoding = "Content-Encoding";
inline constexpr std::string_view H_ETag            = "ETag";
inline constexpr std::string_view H_LastModified    = "Last-Modified";
inline constexpr std::string_view H_Range           = "Range";
inline constexpr std::string_view H_ContentRange    = "Content-Range";
inline constexpr std::string_view H_CacheControl    = "Cache-Control";
inline constexpr std::string_view H_XRequestId      = "X-Request-Id";

// ------------------------------
// MIME helpers
// ------------------------------

/**
 * @brief MIME type for a file extension (including the dot, e.g. ".html").
 * @return The MIME type, or "application/octet-stream" when unknown.
 */
std::string_view mime_from_ext(std::string_view ext);

/** @brief MIME type inferred from the extension of @p path. */
std::string_view content_type_for_path(std::string_view path);

} // namespace socketify
