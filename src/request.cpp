#include "request.h"
#include <sstream>

namespace socketify {

// in real implementation you’d stream from socket/buffer.
// here we stub with placeholder.
std::string Request::body() {
    // In a real server, this would be filled during parsing
    return {};
}

std::optional<nlohmann::json> Request::json() {
    if (!is_json_ct(content_type)) return std::nullopt;
    try {
        return nlohmann::json::parse(body());
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace socketify
