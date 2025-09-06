// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace socketify {

inline bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(),
                      [](char x, char y){ return std::tolower((unsigned char)x) == std::tolower((unsigned char)y); });
}

// RFC 7231-ish content-type helpers
inline bool is_json_ct(std::string_view ct) {
    // handles: application/json; charset=utf-8
    return ct.find("application/json") != std::string_view::npos ||
           ct.find("+json") != std::string_view::npos;
}

} // namespace socketify
