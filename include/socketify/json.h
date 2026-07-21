#pragma once
/**
 * @file json.h
 * @brief Ergonomic helpers over nlohmann::json: safe parsing and typed
 *        dotted-path access with structured errors.
 *
 * The framework already exposes nlohmann::json everywhere (request/response,
 * ORM rows, sessions). This wrapper adds a thin, exception-light layer for the
 * common web-handler patterns.
 *
 * @code
 * auto doc = json_util::parse(req.body_view());
 * if (!doc) { res.json_error(Status::BadRequest, "invalid json"); return; }
 *
 * auto name = json_util::get<std::string>(*doc, "user.name");     // std::optional
 * int  age  = json_util::get_or<int>(*doc, "user.age", 0);        // with fallback
 * auto id   = json_util::require<std::int64_t>(*doc, "id");       // throws json_util::Error
 * @endcode
 */

#include <nlohmann/json.hpp>

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace socketify::json_util {

/** @brief Convenient alias for the underlying JSON value type. */
using Value = nlohmann::json;

/** @brief Raised by require() when a path is missing or has the wrong type. */
class Error : public std::runtime_error {
public:
    Error(std::string path, const std::string& message)
        : std::runtime_error(message), path_(std::move(path)) {}

    /** @brief The dotted path that failed (e.g. "user.name"). */
    const std::string& path() const noexcept { return path_; }

private:
    std::string path_;
};

/**
 * @brief Parse @p text as JSON without throwing.
 * @return The document, or std::nullopt when empty or invalid.
 */
std::optional<nlohmann::json> parse(std::string_view text);

/** @brief Parse @p text, returning @p fallback when empty or invalid. */
nlohmann::json parse_or(std::string_view text, nlohmann::json fallback);

/**
 * @brief Resolve a dotted path against @p root.
 *
 * Segments separated by '.' descend into objects; a numeric segment indexes
 * into an array. Returns nullptr when any segment is missing.
 *
 * @return Pointer into @p root (valid while @p root lives), or nullptr.
 */
const nlohmann::json* find(const nlohmann::json& root, std::string_view path);

/** @brief True when @p path resolves to a non-null value. */
inline bool has(const nlohmann::json& root, std::string_view path) {
    const auto* p = find(root, path);
    return p != nullptr && !p->is_null();
}

/**
 * @brief Typed access at @p path.
 * @return The value as T, or std::nullopt when missing/null or not convertible.
 */
template <typename T>
std::optional<T> get(const nlohmann::json& root, std::string_view path) {
    const auto* p = find(root, path);
    if (p == nullptr || p->is_null()) return std::nullopt;
    try {
        return p->get<T>();
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

/** @brief Typed access at @p path with a fallback when missing/wrong type. */
template <typename T>
T get_or(const nlohmann::json& root, std::string_view path, T fallback) {
    auto v = get<T>(root, path);
    return v ? *v : fallback;
}

/**
 * @brief Typed access at @p path; throws json_util::Error when missing or wrong type.
 */
template <typename T>
T require(const nlohmann::json& root, std::string_view path) {
    const auto* p = find(root, path);
    if (p == nullptr || p->is_null()) {
        throw Error(std::string(path), "missing required field '" + std::string(path) + "'");
    }
    try {
        return p->get<T>();
    } catch (const nlohmann::json::exception&) {
        throw Error(std::string(path), "field '" + std::string(path) + "' has the wrong type");
    }
}

} // namespace socketify::json_util
