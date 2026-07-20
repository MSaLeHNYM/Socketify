/**
 * @file cookies.cpp
 * @brief Cookie header parsing and Set-Cookie serialization.
 */

#include "socketify/cookies.h"
#include "socketify/detail/utils.h"

namespace socketify {

std::string Cookie::to_string() const {
    std::string out;
    out.reserve(64 + name_.size() + value_.size());
    out.append(name_).push_back('=');
    out.append(value_);

    if (!path_.empty()) out.append("; Path=").append(path_);
    if (!domain_.empty()) out.append("; Domain=").append(domain_);
    if (max_age_) out.append("; Max-Age=").append(std::to_string(*max_age_));
    if (expires_) out.append("; Expires=").append(detail::http_date(*expires_));
    if (secure_) out.append("; Secure");
    if (http_only_) out.append("; HttpOnly");
    if (same_site_) {
        out.append("; SameSite=");
        switch (*same_site_) {
            case SameSite::None:   out.append("None"); break;
            case SameSite::Lax:    out.append("Lax"); break;
            case SameSite::Strict: out.append("Strict"); break;
        }
    }
    return out;
}

namespace cookies {

void parse_cookie_header(std::string_view header,
                         std::unordered_map<std::string, std::string>& out) {
    for (auto pair : detail::split_view(header, ';')) {
        pair = detail::trim_view(pair);
        if (pair.empty()) continue;
        auto eq = pair.find('=');
        if (eq == std::string_view::npos || eq == 0) continue;
        std::string_view key = detail::trim_view(pair.substr(0, eq));
        std::string_view val = detail::trim_view(pair.substr(eq + 1));
        // Strip surrounding double quotes if present.
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
            val = val.substr(1, val.size() - 2);
        }
        if (!key.empty()) out[std::string(key)] = std::string(val);
    }
}

std::string expired(std::string_view name, std::string_view path) {
    return Cookie(std::string(name), "")
        .path(std::string(path))
        .max_age(0)
        .expires(0)
        .to_string();
}

} // namespace cookies

} // namespace socketify
