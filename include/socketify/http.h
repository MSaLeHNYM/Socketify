#pragma once
// socketify/http.h — core HTTP enums, types, constants, helpers

#include <string>
#include <string_view>
#include <unordered_map>
#include <cstddef>
#include <cstdint>

namespace socketify {

// ------------------------------
// Case-insensitive string utils
// ------------------------------
/**
 * @brief Case-insensitive hash function for strings.
 */
struct ci_hash {
    /**
     * @brief Computes the hash of a string in a case-insensitive manner.
     * @param s The string to hash.
     * @return The hash value.
     */
    std::size_t operator()(std::string_view s) const noexcept;
};

/**
 * @brief Case-insensitive equality comparison for strings.
 */
struct ci_equal {
    /**
     * @brief Compares two strings for equality in a case-insensitive manner.
     * @param a The first string.
     * @param b The second string.
     * @return true if the strings are equal, false otherwise.
     */
    bool operator()(std::string_view a, std::string_view b) const noexcept;
};

// ------------------------------
// Header map (case-insensitive keys)
// ------------------------------
/**
 * @brief A map for HTTP headers with case-insensitive keys.
 */
using HeaderMap = std::unordered_map<std::string, std::string, ci_hash, ci_equal>;

// ------------------------------
// Methods
// ------------------------------
/**
 * @brief Enum representing HTTP methods.
 */
enum class Method {
    UNKNOWN = 0,
    GET, POST, PUT, PATCH,
    DELETE_,        // maps to "DELETE"
    OPTIONS, HEAD,
    CONNECT, TRACE, // needed by http.cpp
    ANY
};

/**
 * @brief Converts an HTTP method enum to its string representation.
 * @param m The HTTP method.
 * @return A string view of the method.
 */
std::string_view to_string(Method m);
/**
 * @brief Converts a string to an HTTP method enum.
 * @param s The string representation of the method.
 * @return The corresponding Method enum value.
 */
Method method_from_string(std::string_view s);

// ------------------------------
// Status codes
// ------------------------------
/**
 * @brief Enum representing HTTP status codes.
 */
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
    NotModified         = 304,
    MovedPermanently    = 301,
    Found               = 302,
    SeeOther            = 303,
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
    RangeNotSatisfiable = 416,
    TooManyRequests     = 429,

    // 5xx
    InternalServerError = 500,
    NotImplemented      = 501,
    BadGateway          = 502,
    ServiceUnavailable  = 503,
    GatewayTimeout      = 504
};

/**
 * @brief Gets the reason phrase for an HTTP status code.
 * @param s The HTTP status code.
 * @return A string view of the reason phrase.
 */
std::string_view reason(Status s);

// ------------------------------
// Common header names (constants)
// ------------------------------
inline constexpr std::string_view H_ContentLength   = "Content-Length";
inline constexpr std::string_view H_ContentType     = "Content-Type";
inline constexpr std::string_view H_Connection      = "Connection";
inline constexpr std::string_view H_SetCookie       = "Set-Cookie";
inline constexpr std::string_view H_Location        = "Location";
inline constexpr std::string_view H_TransferEncoding= "Transfer-Encoding";
inline constexpr std::string_view H_AcceptEncoding  = "Accept-Encoding";
inline constexpr std::string_view H_ContentEncoding = "Content-Encoding";
inline constexpr std::string_view H_ETag            = "ETag";
inline constexpr std::string_view H_LastModified    = "Last-Modified";
inline constexpr std::string_view H_Range           = "Range";
inline constexpr std::string_view H_ContentRange    = "Content-Range";

// ------------------------------
// MIME helpers (declared; defined in src/http.cpp)
// ------------------------------
/**
 * @brief Gets the MIME type for a file extension.
 * @param ext The file extension.
 * @return A string view of the MIME type.
 */
std::string_view mime_from_ext(std::string_view ext);
/**
 * @brief Gets the Content-Type for a file path.
 * @param path The file path.
 * @return A string view of the Content-Type.
 */
std::string_view content_type_for_path(std::string_view path);

} // namespace socketify