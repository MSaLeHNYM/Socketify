#pragma once
// socketify/compression.h — gzip/deflate helpers using zlib

#include <string>
#include <string_view>
#include <vector>

namespace socketify::compression {

/**
 * @brief Enum representing supported compression encodings.
 */
enum class Encoding {
    None,
    Gzip,
    Deflate
};

/**
 * @brief Holds configuration options for compression.
 */
struct Options {
    /**
     * @brief Whether to enable compression.
     */
    bool enable{true};
    /**
     * @brief Whether to enable gzip compression.
     */
    bool enable_gzip{true};
    /**
     * @brief Whether to enable deflate compression.
     */
    bool enable_deflate{true};
    /**
     * @brief The minimum size of a response body to be eligible for compression.
     */
    std::size_t min_size{256};          // don't compress tiny bodies

    // Optional allowlist of content-types (prefix match). Empty -> try all except
    // already compressed/binary-ish ones (e.g., image/*, video/*, application/zip).
    /**
     * @brief A list of compressible content types.
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

/**
 * @brief Checks if a content type is compressible.
 * @param ct The content type.
 * @param opts The compression options.
 * @return true if the content type is compressible, false otherwise.
 */
bool is_compressible_type(std::string_view ct, const Options& opts);

/**
 * @brief Negotiates the best encoding based on the Accept-Encoding header.
 * @param accept_enc The value of the Accept-Encoding header.
 * @param opts The compression options.
 * @return The negotiated encoding.
 */
Encoding negotiate_accept_encoding(std::string_view accept_enc, const Options& opts);

/**
 * @brief Compresses data using gzip.
 * @param src The source data.
 * @param out The output string.
 * @param level The compression level.
 * @return true on success, false otherwise.
 */
bool gzip_compress(std::string_view src, std::string& out, int level = -1);     // -1 = zlib default
/**
 * @brief Compresses data using deflate.
 * @param src The source data.
 * @param out The output string.
 * @param level The compression level.
 * @return true on success, false otherwise.
 */
bool deflate_compress(std::string_view src, std::string& out, int level = -1);

} // namespace socketify::compression