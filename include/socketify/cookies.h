#pragma once
// socketify/cookies.h — Cookie parsing

#include <string_view>
#include <unordered_map>

namespace socketify {

// Parses a "Cookie" header string into a key-value map.
//
// Example: "key1=val1; key2=val2"
//
// - out_cookies: Map to store the parsed cookies.
// - cookie_header: The raw header value.
//
// Note: This is a simplified parser. It doesn't handle complex cases
// like quoted values perfectly, but is sufficient for most uses.
void parse_cookie_header(
    std::unordered_map<std::string, std::string>& out_cookies,
    std::string_view cookie_header);

} // namespace socketify