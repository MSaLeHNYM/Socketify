#pragma once
/**
 * @file compression.h
 * @brief Response compression: gzip/deflate negotiation and zlib helpers.
 *
 * The server compresses buffered responses automatically when
 * ServerOptions::compression.enable is true and the client sends a
 * matching Accept-Encoding header.
 */

#include <string>
#include <string_view>
#include <vector>

namespace socketify::compression {

/** @brief Supported content encodings. */
enum class Encoding {
    None,   ///< No compression.
    Gzip,   ///< RFC 1952 gzip.
    Deflate ///< RFC 1950 zlib ("deflate").
};

/** @brief Compression policy. */
struct Options {
    /** @brief Master switch. */
    bool enable{true};
    /** @brief Offer gzip. */
    bool enable_gzip{true};
    /** @brief Offer deflate. */
    bool enable_deflate{true};
    /** @brief Skip bodies smaller than this many bytes. */
    std::size_t min_size{256};

    /**
     * @brief Content-Type allowlist (prefix match). Compression is applied
     *        only when the response type matches one of these prefixes.
     */
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

/** @brief True when @p ct matches the compressible-type allowlist. */
bool is_compressible_type(std::string_view ct, const Options& opts);

/**
 * @brief Choose the best encoding for an Accept-Encoding header value.
 * @return Encoding::None when the client accepts none of the enabled ones.
 */
Encoding negotiate_accept_encoding(std::string_view accept_enc, const Options& opts);

/**
 * @brief gzip-compress @p src into @p out.
 * @param src   Bytes to compress.
 * @param out   Receives the compressed payload.
 * @param level zlib level 0-9; -1 selects the zlib default.
 * @return true on success.
 */
bool gzip_compress(std::string_view src, std::string& out, int level = -1);

/** @brief deflate-compress @p src into @p out. @see gzip_compress */
bool deflate_compress(std::string_view src, std::string& out, int level = -1);

} // namespace socketify::compression
