#include "socketify/cookies.h"
#include "socketify/detail/utils.h" // for trim

namespace socketify {

void parse_cookie_header(
    std::unordered_map<std::string, std::string>& out_cookies,
    std::string_view cookie_header) {

    size_t start = 0;
    while (start < cookie_header.length()) {
        // Find the next cookie pair
        size_t end = cookie_header.find(';', start);
        if (end == std::string_view::npos) {
            end = cookie_header.length();
        }

        std::string_view cookie_pair = cookie_header.substr(start, end - start);

        // Find the separator
        size_t separator = cookie_pair.find('=');
        if (separator != std::string_view::npos) {
            std::string_view key = cookie_pair.substr(0, separator);
            std::string_view value = cookie_pair.substr(separator + 1);

            key = detail::trim(key);
            value = detail::trim(value); // Basic trim

            // Simplified value decoding (no full URL decoding for this stub)
            if (value.length() >= 2 && value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.length() - 2);
            }

            if (!key.empty()) {
                out_cookies[std::string(key)] = std::string(value);
            }
        }

        start = end + 1;
    }
}

} // namespace socketify