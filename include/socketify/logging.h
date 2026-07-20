#pragma once
/**
 * @file logging.h
 * @brief Leveled logger and request-logging middleware.
 *
 * @code
 * logging::set_level(logging::Level::Debug);
 * server.Use(logging::middleware());          // "GET /api/x 200 1.2ms 34B"
 *
 * logging::info("listening on :{}", 8080);    // printf-free formatting
 * @endcode
 */

#include "socketify/middleware.h"

#include <functional>
#include <sstream>
#include <string>
#include <string_view>

namespace socketify::logging {

/** @brief Log severities, lowest to highest. */
enum class Level { Trace, Debug, Info, Warn, Error, Off };

/** @brief Sink invoked for every emitted record (already formatted). */
using Sink = std::function<void(Level, std::string_view message)>;

/** @brief Set the minimum level that will be emitted (default Info). */
void set_level(Level lvl);
/** @brief Current minimum level. */
Level level();

/**
 * @brief Replace the output sink (default writes to stderr with a
 *        timestamp and level tag). Pass nullptr to restore the default.
 * @note Thread-safe; the sink itself must be thread-safe.
 */
void set_sink(Sink sink);

/** @brief Emit a preformatted record at @p lvl. */
void log(Level lvl, std::string_view message);

/// @cond INTERNAL
namespace fmt_detail {
inline void append_one(std::ostringstream& os, std::string_view s) { os << s; }
inline void append_one(std::ostringstream& os, const char* s) { os << s; }
inline void append_one(std::ostringstream& os, const std::string& s) { os << s; }
template <typename T>
inline void append_one(std::ostringstream& os, const T& v) { os << v; }

template <typename... Args>
std::string format(std::string_view pattern, const Args&... args) {
    std::ostringstream os;
    std::size_t pos = 0;
    auto emit = [&](const auto& v) {
        std::size_t brace = pattern.find("{}", pos);
        if (brace == std::string_view::npos) return;
        os << pattern.substr(pos, brace - pos);
        append_one(os, v);
        pos = brace + 2;
    };
    (emit(args), ...);
    os << pattern.substr(pos);
    return os.str();
}
} // namespace fmt_detail
/// @endcond

/** @brief Log at Trace level; "{}" placeholders are replaced in order. */
template <typename... Args>
void trace(std::string_view pattern, const Args&... args) {
    if (level() <= Level::Trace) log(Level::Trace, fmt_detail::format(pattern, args...));
}
/** @brief Log at Debug level. */
template <typename... Args>
void debug(std::string_view pattern, const Args&... args) {
    if (level() <= Level::Debug) log(Level::Debug, fmt_detail::format(pattern, args...));
}
/** @brief Log at Info level. */
template <typename... Args>
void info(std::string_view pattern, const Args&... args) {
    if (level() <= Level::Info) log(Level::Info, fmt_detail::format(pattern, args...));
}
/** @brief Log at Warn level. */
template <typename... Args>
void warn(std::string_view pattern, const Args&... args) {
    if (level() <= Level::Warn) log(Level::Warn, fmt_detail::format(pattern, args...));
}
/** @brief Log at Error level. */
template <typename... Args>
void error(std::string_view pattern, const Args&... args) {
    if (level() <= Level::Error) log(Level::Error, fmt_detail::format(pattern, args...));
}

/** @brief Request-log middleware configuration. */
struct Options {
    /**
     * @brief Format style:
     *  - "dev": `GET /path 200 1.234ms 56B` (default)
     *  - "common": Apache common-log style with client IP and timestamp.
     */
    std::string format{"dev"};
    /** @brief Level used for the records (default Info). */
    Level log_level{Level::Info};
};

/**
 * @brief Middleware that logs each request after the handler finishes.
 *
 * Records method, path, status, handler duration and response size.
 */
Middleware middleware(Options opts = {});

} // namespace socketify::logging
