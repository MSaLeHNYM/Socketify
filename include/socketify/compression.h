#pragma once
// socketify/compression.h — gzip/deflate helpers using zlib

#include <string>
#include <string_view>
#include <vector>

namespace socketify::compression {

enum class Encoding {
    None,
    Gzip,
    Deflate
};

struct Options {
    bool enable{true};
    bool enable_gzip{true};
    bool enable_deflate{true};
    std::size_t min_size{256};          // don't compress tiny bodies

    // Optional allowlist of content-types (prefix match). Empty -> try all except
    // already compressed/binary-ish ones (e.g., image/*, video/*, application/zip).
    std::vector<std::string> compressible_types{
        "text/",
        "application/json",
        "application/javascript",
        "application/xml",
        "application/xhtml+xml",
        "application/rss+xml",
        "image/svg+xml"
    };
};

// Return true if "ct" appears compressible given allowlist (prefix match).
bool is_compressible_type(std::string_view ct, const Options& opts);

// Parse Accept-Encoding and choose best encoding supported by opts.
// Returns Encoding::None if not acceptable.
Encoding negotiate_accept_encoding(std::string_view accept_enc, const Options& opts);

// zlib helpers. Returns true on success and fills out.
bool gzip_compress(std::string_view src, std::string& out, int level = -1);     // -1 = zlib default
bool deflate_compress(std::string_view src, std::string& out, int level = -1);

} // namespace socketify::compression
