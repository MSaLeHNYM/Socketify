#pragma once
/**
 * @file config.h
 * @brief Typed configuration from environment variables and .env files.
 *
 * @code
 * using namespace socketify;
 * auto cfg = config::Config::from_env().merge_file(".env");
 *
 * int port          = cfg.get_int("PORT").value_or(8080);
 * bool debug        = cfg.get_bool("DEBUG").value_or(false);
 * std::string secret = cfg.require("SESSION_SECRET");   // throws if missing
 * @endcode
 */

#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace socketify::config {

/** @brief Raised by require*() when a key is missing. */
class Error : public std::runtime_error {
public:
    explicit Error(const std::string& key)
        : std::runtime_error("missing required config key '" + key + "'"), key_(key) {}
    const std::string& key() const noexcept { return key_; }

private:
    std::string key_;
};

/**
 * @brief An immutable-ish key/value configuration store.
 *
 * Values come from the process environment and/or parsed .env files. Later
 * sources override earlier ones only when @p overrides is true.
 */
class Config {
public:
    Config() = default;

    /** @brief Snapshot the current process environment. */
    static Config from_env();

    /** @brief Load only from a .env file (missing file yields an empty Config). */
    static Config from_file(const std::string& path);

    /**
     * @brief Merge keys parsed from @p path into this Config.
     * @param overrides When true, file values replace existing keys.
     * @return *this (chainable).
     */
    Config& merge_file(const std::string& path, bool overrides = false);

    /** @brief Set/override a single key. */
    Config& set(std::string key, std::string value);

    bool contains(const std::string& key) const { return values_.count(key) > 0; }

    /** @brief Raw string value. */
    std::optional<std::string> get(const std::string& key) const;
    /** @brief Raw string value or @p fallback. */
    std::string get_or(const std::string& key, std::string fallback) const;

    std::optional<long long> get_int(const std::string& key) const;
    std::optional<double> get_double(const std::string& key) const;
    /** @brief Parses 1/true/yes/on (case-insensitive) as true; 0/false/no/off as false. */
    std::optional<bool> get_bool(const std::string& key) const;

    /** @brief String value or throw config::Error. */
    std::string require(const std::string& key) const;
    /** @brief Integer value or throw config::Error (missing or unparsable). */
    long long require_int(const std::string& key) const;

    /** @brief All key/value pairs (for logging/debugging). */
    const std::map<std::string, std::string>& all() const noexcept { return values_; }

    /** @brief Parse .env text into key/value pairs (exposed for testing). */
    static std::map<std::string, std::string> parse_env(std::string_view text);

private:
    std::map<std::string, std::string> values_;
};

} // namespace socketify::config
