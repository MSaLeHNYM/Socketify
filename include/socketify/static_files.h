#pragma once
/**
 * @file static_files.h
 * @brief Static file serving middleware with caching, ranges and streaming.
 *
 * Files are streamed with sendfile(2) on plain sockets (no in-memory copy)
 * and support ETag/Last-Modified conditional requests plus single Range
 * requests.
 *
 * @code
 * server.Use(static_files::serve("public", {.mount = "/assets",
 *                                           .cache_max_age = 3600}));
 * @endcode
 */

#include "socketify/http.h"
#include "socketify/middleware.h"
#include "socketify/request.h"
#include "socketify/response.h"

#include <string>
#include <string_view>
#include <vector>

namespace socketify::static_files {

/** @brief Static file middleware configuration. */
struct Options {
    // Filesystem root to serve from. REQUIRED (we also expose a factory overload that fills this).
    std::string root;

    // Mount path prefix (URL path that this middleware handles), default "/".
    // Examples: "/", "/assets"
    std::string mount{"/"};

    // If true (default), when a path doesn't map to a file here, call next().
    // If false, respond 404 directly.
    bool fallthrough{true};

    // Auto-serve index files when a directory is requested (e.g., /docs/ -> /docs/index.html).
    bool auto_index{true};

    // Filenames considered as index (first existing wins).
    std::vector<std::string> index_names{"index.html", "index.htm"};

    // If true, render a simple HTML listing for directories that don’t have an index.
    bool directory_listing{false};

    // If false, block paths with any dotfile segment (".hidden").
    bool allow_hidden{false};

    // Caching
    bool etag{true};
    bool last_modified{true};
    int  cache_max_age{0};     // seconds; 0 => no Cache-Control emitted
    bool immutable{false};     // add ", immutable" to Cache-Control

    // Content-Type detection is built-in (by file extension).
};

/** @brief Create the middleware; @p opts.root must be set. */
Middleware serve(Options opts);

/** @brief Convenience overload: serve(root, other-options). */
inline Middleware serve(const std::string& root, Options opts = {}) {
    Options o = std::move(opts);
    o.root = root;
    return serve(std::move(o));
}

} // namespace socketify::static_files
