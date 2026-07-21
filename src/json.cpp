/**
 * @file json.cpp
 * @brief JSON parsing + dotted-path resolution.
 */

#include "socketify/json.h"

#include "socketify/detail/utils.h"

namespace socketify::json_util {

std::optional<nlohmann::json> parse(std::string_view text) {
    if (text.empty()) return std::nullopt;
    auto j = nlohmann::json::parse(text, /*cb=*/nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) return std::nullopt;
    return j;
}

nlohmann::json parse_or(std::string_view text, nlohmann::json fallback) {
    auto j = parse(text);
    return j ? std::move(*j) : std::move(fallback);
}

namespace {

bool is_all_digits(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

} // namespace

const nlohmann::json* find(const nlohmann::json& root, std::string_view path) {
    const nlohmann::json* cur = &root;
    // An empty path refers to the root itself.
    if (path.empty()) return cur;

    for (std::string_view seg : detail::split_view(path, '.')) {
        if (seg.empty()) return nullptr;
        if (cur->is_object()) {
            auto it = cur->find(std::string(seg));
            if (it == cur->end()) return nullptr;
            cur = &(*it);
        } else if (cur->is_array() && is_all_digits(seg)) {
            std::size_t idx = 0;
            for (char c : seg) idx = idx * 10 + static_cast<std::size_t>(c - '0');
            if (idx >= cur->size()) return nullptr;
            cur = &(*cur)[idx];
        } else {
            return nullptr;
        }
    }
    return cur;
}

} // namespace socketify::json_util
