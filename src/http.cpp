// src/http.cpp
#include "socketify/http.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>

namespace socketify {

// ------------------------------
// case-insensitive helpers
// ------------------------------
static inline char tolower_ascii(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc >= 'A' && uc <= 'Z') return static_cast<char>(uc - 'A' + 'a');
    return static_cast<char>(c);
}

std::size_t ci_hash::operator()(std::string_view s) const noexcept {
    // FNV-1a with lowercased ascii
    std::size_t h = 1469598103934665603ull;
    for (char c : s) {
        h ^= static_cast<unsigned char>(tolower_ascii(c));
        h *= 1099511628211ull;
    }
    return h;
}

bool ci_equal::operator()(std::string_view a, std::string_view b) const noexcept {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (tolower_ascii(a[i]) != tolower_ascii(b[i])) return false;
    }
    return true;
}

// ------------------------------
// Methods
// ------------------------------
std::string_view to_string(Method m) {
    switch (m) {
        case Method::GET:      return "GET";
        case Method::POST:     return "POST";
        case Method::PUT:      return "PUT";
        case Method::PATCH:    return "PATCH";
        case Method::DELETE_:  return "DELETE";
        case Method::OPTIONS:  return "OPTIONS";
        case Method::HEAD:     return "HEAD";
        case Method::CONNECT:  return "CONNECT";
        case Method::TRACE:    return "TRACE";
        case Method::ANY:      return "ANY";
        default:               return "UNKNOWN";
    }
}

static inline bool ieq(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (tolower_ascii(a[i]) != tolower_ascii(b[i])) return false;
    return true;
}

Method method_from_string(std::string_view s) {
    if (ieq(s, "GET"))      return Method::GET;
    if (ieq(s, "POST"))     return Method::POST;
    if (ieq(s, "PUT"))      return Method::PUT;
    if (ieq(s, "PATCH"))    return Method::PATCH;
    if (ieq(s, "DELETE"))   return Method::DELETE_;
    if (ieq(s, "OPTIONS"))  return Method::OPTIONS;
    if (ieq(s, "HEAD"))     return Method::HEAD;
    if (ieq(s, "TRACE"))    return Method::TRACE;
    if (ieq(s, "CONNECT"))  return Method::CONNECT;
    return Method::UNKNOWN;
}

// ------------------------------
// Status → reason phrase
// ------------------------------
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
        case Status::RangeNotSatisfiable: return "Range Not Satisfiable";
        case Status::TooManyRequests:     return "Too Many Requests";

        // 5xx
        case Status::InternalServerError: return "Internal Server Error";
        case Status::NotImplemented:      return "Not Implemented";
        case Status::BadGateway:          return "Bad Gateway";
        case Status::ServiceUnavailable:  return "Service Unavailable";
        case Status::GatewayTimeout:      return "Gateway Timeout";
        default:                          return "Unknown";
    }
}

// ------------------------------
// MIME helpers
// ------------------------------
static constexpr std::string_view kDefaultMime = "application/octet-stream";

std::string_view mime_from_ext(std::string_view ext) {
    // expect ext with dot, e.g. ".html"
    // small table; extend as needed
    struct Pair { std::string_view ext; std::string_view mime; };
    static constexpr Pair table[] = {
        {".html", "text/html; charset=utf-8"},
        {".htm",  "text/html; charset=utf-8"},
        {".css",  "text/css; charset=utf-8"},
        {".js",   "application/javascript; charset=utf-8"},
        {".mjs",  "application/javascript; charset=utf-8"},
        {".json", "application/json; charset=utf-8"},
        {".svg",  "image/svg+xml"},
        {".png",  "image/png"},
        {".jpg",  "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif",  "image/gif"},
        {".webp", "image/webp"},
        {".txt",  "text/plain; charset=utf-8"},
        {".xml",  "application/xml; charset=utf-8"},
        {".pdf",  "application/pdf"},
        {".wasm", "application/wasm"},
        {".ico",  "image/x-icon"}
    };

    // lowercase compare
    auto lower = [](std::string_view s) {
        std::string out(s);
        for (auto& c : out) c = tolower_ascii(c);
        return out;
    };
    std::string e = lower(ext);
    for (const auto& p : table) {
        if (e == p.ext) return p.mime;
    }
    return kDefaultMime;
}

std::string_view content_type_for_path(std::string_view path) {
    auto dot   = path.rfind('.');
    auto slash = path.rfind('/');
    if (dot == std::string_view::npos) return kDefaultMime;
    if (slash != std::string_view::npos && dot < slash) return kDefaultMime;
    return mime_from_ext(path.substr(dot));
}

} // namespace socketify
