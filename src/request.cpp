#include "socketify/request.h"
#include "socketify/detail/utils.h"

#include <algorithm>
#include <cctype>

namespace socketify {

/**
 * @brief Gets a specific header value.
 * @param key The header key.
 * @return The header value.
 */
std::string_view Request::header(std::string_view key) const {
    auto it = headers_.find(std::string(key));
    if (it == headers_.end()) return {};
    return it->second;
}

/**
 * @brief Gets a specific cookie value.
 * @param key The cookie key.
 * @return The cookie value.
 */
std::string_view Request::cookie(std::string_view key) const {
    auto it = cookies_.find(std::string(key));
    if (it == cookies_.end()) return {};
    return it->second;
}

} // namespace socketify