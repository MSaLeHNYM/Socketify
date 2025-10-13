#pragma once
// socketify/static_files.h — Serve static files (v1)

#include "socketify/http.h"
#include "socketify/request.h"
#include "socketify/response.h"
#include "socketify/router.h"

#include <string>
#include <string_view>
#include <vector>

namespace socketify::static_files {

/**
 * @brief Holds configuration options for serving static files.
 */
struct Options {
    /**
     * @brief The filesystem root to serve files from.
     */
    std::string root;

    /**
     * @brief The URL path that this middleware handles.
     */
    std::string mount{"/"};

    /**
     * @brief Whether to call the next middleware if a file is not found.
     */
    bool fallthrough{true};

    /**
     * @brief Whether to automatically serve index files when a directory is requested.
     */
    bool auto_index{true};

    /**
     * @brief A list of filenames to be considered as index files.
     */
    std::vector<std::string> index_names{"index.html", "index.htm"};

    /**
     * @brief Whether to render a directory listing for directories that don't have an index file.
     */
    bool directory_listing{false};

    /**
     * @brief Whether to allow serving hidden files.
     */
    bool allow_hidden{false};

    /**
     * @brief Whether to generate ETag headers.
     */
    bool etag{true};
    /**
     * @brief Whether to generate Last-Modified headers.
     */
    bool last_modified{true};
    /**
     * @brief The value for the Cache-Control max-age directive.
     */
    int  cache_max_age{0};     // seconds; 0 => no Cache-Control emitted
    /**
     * @brief Whether to add the "immutable" directive to the Cache-Control header.
     */
    bool immutable{false};     // add ", immutable" to Cache-Control

    // Content-Type detection is built-in (by file extension).
};

/**
 * @brief Creates a new middleware for serving static files.
 * @param opts The options for serving static files.
 * @return A middleware function.
 */
Middleware serve(Options opts);

/**
 * @brief Creates a new middleware for serving static files.
 * @param root The filesystem root to serve files from.
 * @param opts The options for serving static files.
 * @return A middleware function.
 */
inline Middleware serve(const std::string& root, Options opts = {}) {
    Options o = std::move(opts);
    o.root = root;
    return serve(std::move(o));
}

} // namespace socketify::static_files