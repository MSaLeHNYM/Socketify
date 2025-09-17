// socketify/src/http.cpp — implementation for http.h

#include "socketify/http.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace socketify {

// --------- small string helpers (ASCII only) ----------
static inline char ascii_lower(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc >= 'A' && uc <= 'Z') return static_cast<char>(uc - 'A' + 'a');
    return static_cast<char>(uc);
}

static inline bool iequal_ascii(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (ascii_lower(a[i]) != ascii_lower(b[i])) return false;
    }
    return true;
}

std::size_t ci_hash::operator()(std::string_view s) const noexcept {
    // Simple Fowler–Noll–Vo (FNV-1a) with ASCII lower
    std::size_t h = 1469598103934665603ull;
    for (char c : s) {
        h ^= static_cast<unsigned char>(ascii_lower(c));
        h *= 1099511628211ull;
    }
    return h;
}
bool ci_equal::operator()(std::string_view a, std::string_view b) const noexcept {
    return iequal_ascii(a, b);
}

// --------- Method <-> string ----------
std::string_view to_string(Method m) {
    switch (m) {
        case Method::GET:     return "GET";
        case Method::POST:    return "POST";
        case Method::PUT:     return "PUT";
        case Method::PATCH:   return "PATCH";
        case Method::DELETE_: return "DELETE";
        case Method::HEAD:    return "HEAD";
        case Method::OPTIONS: return "OPTIONS";
        case Method::CONNECT: return "CONNECT";
        case Method::TRACE:   return "TRACE";
        case Method::ANY:     return "*";
        case Method::UNKNOWN: default: return "UNKNOWN";
    }
}

Method method_from_string(std::string_view s) {
    // Compare by length first (fast path), case-insensitive match
    switch (s.size()) {
        case 3:
            if (iequal_ascii(s, "GET")) return Method::GET;
            if (iequal_ascii(s, "PUT")) return Method::PUT;
            break;
        case 4:
            if (iequal_ascii(s, "POST")) return Method::POST;
            if (iequal_ascii(s, "HEAD")) return Method::HEAD;
            break;
        case 5:
            if (iequal_ascii(s, "PATCH")) return Method::PATCH;
            if (iequal_ascii(s, "TRACE")) return Method::TRACE;
            break;
        case 6:
            if (iequal_ascii(s, "DELETE")) return Method::DELETE_;
            break;
        case 7:
            if (iequal_ascii(s, "OPTIONS")) return Method::OPTIONS;
            if (iequal_ascii(s, "CONNECT")) return Method::CONNECT;
            break;
        case 1:
            if (s[0] == '*') return Method::ANY;
            break;
        default:
            break;
    }
    return Method::UNKNOWN;
}

// --------- Status -> reason phrase ----------
std::string_view reason(Status s) {
    switch (s) {
        // 1xx
        case Status::Continue:            return "Continue";
        case Status::SwitchingProtocols:  return "Switching Protocols";
        case Status::Processing:          return "Processing";

        // 2xx
        case Status::OK:                  return "OK";
        case Status::Created:             return "Created";
        case Status::Accepted:            return "Accepted";
        case Status::NoContent:           return "No Content";
        case Status::PartialContent:      return "Partial Content";

        // 3xx
        case Status::MovedPermanently:    return "Moved Permanently";
        case Status::Found:               return "Found";
        case Status::SeeOther:            return "See Other";
        case Status::NotModified:         return "Not Modified";
        case Status::TemporaryRedirect:   return "Temporary Redirect";
        case Status::PermanentRedirect:   return "Permanent Redirect";

        // 4xx
        case Status::BadRequest:          return "Bad Request";
        case Status::Unauthorized:        return "Unauthorized";
        case Status::Forbidden:           return "Forbidden";
        case Status::NotFound:            return "Not Found";
        case Status::MethodNotAllowed:    return "Method Not Allowed";
        case Status::RequestTimeout:      return "Request Timeout";
        case Status::Conflict:            return "Conflict";
        case Status::Gone:                return "Gone";
        case Status::PayloadTooLarge:     return "Payload Too Large";
        case Status::URITooLong:          return "URI Too Long";
        case Status::UnsupportedMediaType:return "Unsupported Media Type";
        case Status::TooManyRequests:     return "Too Many Requests";

        // 5xx
        case Status::InternalServerError: return "Internal Server Error";
        case Status::NotImplemented:      return "Not Implemented";
        case Status::BadGateway:          return "Bad Gateway";
        case Status::ServiceUnavailable:  return "Service Unavailable";
        case Status::GatewayTimeout:      return "Gateway Timeout";
    }
    // Fallback: should never happen with enum coverage
    return "Unknown";
}

// --------- MIME helpers ----------

struct MimeRow { std::string_view ext; std::string_view mime; };
static constexpr MimeRow kMimeTable[] = {
    // html/css/js
    { "html", "text/html" },
    { "htm",  "text/html" },
    { "css",  "text/css" },
    { "js",   "application/javascript" },
    { "mjs",  "application/javascript" },
    // images
    { "png",  "image/png" },
    { "jpg",  "image/jpeg" },
    { "jpeg", "image/jpeg" },
    { "gif",  "image/gif" },
    { "webp", "image/webp" },
    { "svg",  "image/svg+xml" },
    { "ico",  "image/x-icon" },
    // fonts
    { "woff", "font/woff" },
    { "woff2","font/woff2" },
    { "ttf",  "font/ttf" },
    // data
    { "json", "application/json" },
    { "txt",  "text/plain; charset=utf-8" },
    { "xml",  "application/xml" },
    { "pdf",  "application/pdf" },
    { "zip",  "application/zip" },
    // audio/video (basic)
    { "mp3",  "audio/mpeg" },
    { "wav",  "audio/wav" },
    { "mp4",  "video/mp4" },
    { "mov",  "video/quicktime" },
};

static std::string_view lstrip_dot(std::string_view s) {
    if (!s.empty() && s.front() == '.') return s.substr(1);
    return s;
}

std::string_view mime_from_ext(std::string_view ext) {
    ext = lstrip_dot(ext);
    // Linear scan over small constexpr table (fast enough).
    for (const auto& row : kMimeTable) {
        if (iequal_ascii(ext, row.ext)) return row.mime;
    }
    return kDefaultMime;
}

std::string_view content_type_for_path(std::string_view path) {
    // Find last '.' after last '/'
    size_t slash = path.find_last_of('/');
    size_t dot   = path.find_last_of('.');
    if (dot == std::string_view::npos) return kDefaultMime;
    if (slash != std::string_view::npos && dot < slash) return kDefaultMime;
    return mime_from_ext(path.substr(dot + 1));
}

} // namespace socketify
