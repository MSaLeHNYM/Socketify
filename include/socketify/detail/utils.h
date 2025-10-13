#pragma once
// socketify/detail/utils.h — Internal string/crypto/misc helpers

#include <string>
#include <string_view>

namespace socketify::detail {

// --- String helpers ---

// Trim whitespace from both ends of a string_view
inline std::string_view trim(std::string_view s) {
    s.remove_prefix(std::min(s.find_first_not_of(" \t\r\n"), s.size()));
    s.remove_suffix(std::min(s.size() - s.find_last_not_of(" \t\r\n") - 1, s.size()));
    return s;
}

// Case-insensitive character comparison
inline bool iequal_char(char a, char b) {
    return std::tolower(static_cast<unsigned char>(a)) ==
           std::tolower(static_cast<unsigned char>(b));
}

// Case-insensitive string_view comparison
inline bool iequals(std::string_view a, std::string_view b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), socketify::detail::iequal_char);
}


// --- Placeholder crypto helpers ---
// These are NOT secure and are for API demonstration only.
// A real implementation would use a proper crypto library (e.g., OpenSSL).

inline std::string hmac_sha256_placeholder(std::string_view data, std::string_view secret) {
    // WARNING: Insecure placeholder.
    return "signed(" + std::string(data) + "+" + std::string(secret) + ")";
}

inline std::string generate_random_string(size_t length) {
    // WARNING: Insecure placeholder. Not cryptographically random.
    std::string s(length, ' ');
    for (size_t i = 0; i < length; ++i) {
        s[i] = 'a' + (std::rand() % 26);
    }
    return s;
}

} // namespace socketify::detail