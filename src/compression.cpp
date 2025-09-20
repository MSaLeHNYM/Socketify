#include "socketify/compression.h"

#include <algorithm>
#include <zlib.h>

namespace socketify::compression {

static inline char ascii_lower(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc >= 'A' && uc <= 'Z') return static_cast<char>(uc - 'A' + 'a');
    return static_cast<char>(uc);
}
static std::string to_lower(std::string s) { for (auto& c : s) c = ascii_lower(c); return s; }

bool is_compressible_type(std::string_view ct, const Options& opts) {
    if (ct.empty()) return true; // if unknown, allow
    // quick filters for common non-compressible types
    std::string l = to_lower(std::string(ct));
    if (l.rfind("image/", 0) == 0) return false;
    if (l.rfind("video/", 0) == 0) return false;
    if (l.rfind("audio/", 0) == 0) return false;
    if (l == "application/zip" || l == "application/gzip" || l == "application/x-gzip") return false;

    if (opts.compressible_types.empty()) return true;
    for (const auto& p : opts.compressible_types) {
        if (l.rfind(to_lower(p), 0) == 0) return true; // prefix match
    }
    return false;
}

Encoding negotiate_accept_encoding(std::string_view accept_enc, const Options& opts) {
    if (!opts.enable) return Encoding::None;
    if (accept_enc.empty()) return Encoding::None;

    // Simple token scan, weight q-values lightly (prefer gzip then deflate)
    std::string low(accept_enc);
    for (auto& c : low) c = ascii_lower(c);

    auto contains = [&](std::string_view token) {
        return low.find(std::string(token)) != std::string::npos;
    };

    // If client explicitly forbids gzip/deflate with q=0, we won't see a simple token.
    // For v1, we do a simple presence check. (Advanced q-value parsing can be added later.)
    if (opts.enable_gzip && contains("gzip"))    return Encoding::Gzip;
    if (opts.enable_deflate && contains("deflate")) return Encoding::Deflate;

    return Encoding::None;
}

// ---- zlib helpers ----
// gzip: use deflate with gzip wrapper (windowBits = 15 + 16)
bool gzip_compress(std::string_view src, std::string& out, int level) {
    z_stream zs{};
    int wb = 15 + 16; // gzip wrapper
    if (deflateInit2(&zs, level, Z_DEFLATED, wb, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return false;
    }

    out.clear();
    out.reserve(src.size() / 2); // rough guess

    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(src.data()));
    zs.avail_in = static_cast<uInt>(src.size());

    unsigned char buffer[16384];
    int ret;
    do {
        zs.next_out = buffer;
        zs.avail_out = sizeof(buffer);
        ret = deflate(&zs, zs.avail_in ? Z_NO_FLUSH : Z_FINISH);
        if (ret == Z_STREAM_ERROR) {
            deflateEnd(&zs);
            return false;
        }
        std::size_t have = sizeof(buffer) - zs.avail_out;
        out.append(reinterpret_cast<char*>(buffer), have);
    } while (ret != Z_STREAM_END);

    deflateEnd(&zs);
    return true;
}

// deflate: raw zlib stream (RFC 1950 / 1951). Browsers accept "deflate" as zlib stream commonly.
bool deflate_compress(std::string_view src, std::string& out, int level) {
    z_stream zs{};
    int wb = 15; // zlib wrapper
    if (deflateInit2(&zs, level, Z_DEFLATED, wb, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return false;
    }

    out.clear();
    out.reserve(src.size() / 2);

    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(src.data()));
    zs.avail_in = static_cast<uInt>(src.size());

    unsigned char buffer[16384];
    int ret;
    do {
        zs.next_out = buffer;
        zs.avail_out = sizeof(buffer);
        ret = deflate(&zs, zs.avail_in ? Z_NO_FLUSH : Z_FINISH);
        if (ret == Z_STREAM_ERROR) {
            deflateEnd(&zs);
            return false;
        }
        std::size_t have = sizeof(buffer) - zs.avail_out;
        out.append(reinterpret_cast<char*>(buffer), have);
    } while (ret != Z_STREAM_END);

    deflateEnd(&zs);
    return true;
}

} // namespace socketify::compression
