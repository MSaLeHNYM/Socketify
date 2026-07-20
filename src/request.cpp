/**
 * @file request.cpp
 * @brief Request lookups (headers, cookies, query parameters).
 */

#include "socketify/request.h"
#include "socketify/detail/utils.h"

namespace socketify {

std::string_view Request::header(std::string_view key) const {
    auto it = headers_.find(std::string(key));
    if (it == headers_.end()) return {};
    return it->second;
}

std::string_view Request::cookie(std::string_view key) const {
    auto it = cookies_.find(std::string(key));
    if (it == cookies_.end()) return {};
    return it->second;
}

std::string_view Request::query_value(std::string_view key) const {
    auto it = query_.find(std::string(key));
    if (it == query_.end()) return {};
    return it->second;
}

} // namespace socketify
