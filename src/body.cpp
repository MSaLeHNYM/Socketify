#include "socketify/body.h"
#include "socketify/detail/utils.h" // for string search helpers

#include <nlohmann/json.hpp>

namespace socketify {

// ---- JSON Body Parser ----
std::optional<nlohmann::json> parse_json_body(const Request& req) {
    if (!req.has_body()) {
        return std::nullopt;
    }

    // Check content type
    auto content_type = req.header("Content-Type");
    if (content_type.find("application/json") == std::string_view::npos) {
        return std::nullopt;
    }

    try {
        return nlohmann::json::parse(req.body_view());
    } catch (const nlohmann::json::parse_error&) {
        return std::nullopt;
    }
}

// ---- URL-encoded Form Parser ----
namespace {
    // Basic URL decoding. A real implementation would be more robust.
    std::string url_decode(std::string_view str) {
        std::string decoded;
        decoded.reserve(str.length());
        for (size_t i = 0; i < str.length(); ++i) {
            if (str[i] == '%' && i + 2 < str.length()) {
                try {
                    std::string hex = std::string(str.substr(i + 1, 2));
                    char c = static_cast<char>(std::stoi(hex, nullptr, 16));
                    decoded += c;
                    i += 2;
                } catch (...) {
                    decoded += '%'; // Invalid hex
                }
            } else if (str[i] == '+') {
                decoded += ' ';
            } else {
                decoded += str[i];
            }
        }
        return decoded;
    }
}

std::optional<ParamMap> parse_form_body(const Request& req) {
    if (!req.has_body()) {
        return std::nullopt;
    }

    auto content_type = req.header("Content-Type");
    if (content_type.find("application/x-www-form-urlencoded") == std::string_view::npos) {
        return std::nullopt;
    }

    ParamMap params;
    std::string_view body = req.body_view();
    size_t start = 0;
    while (start < body.length()) {
        size_t end = body.find('&', start);
        if (end == std::string_view::npos) {
            end = body.length();
        }

        std::string_view pair = body.substr(start, end - start);
        size_t eq_pos = pair.find('=');

        if (eq_pos != std::string_view::npos) {
            std::string key = url_decode(pair.substr(0, eq_pos));
            std::string value = url_decode(pair.substr(eq_pos + 1));
            if (!key.empty()) {
                params[std::move(key)] = std::move(value);
            }
        } else {
            std::string key = url_decode(pair);
            if (!key.empty()) {
                params[std::move(key)] = "";
            }
        }

        start = end + 1;
    }

    return params;
}

} // namespace socketify