#pragma once
/**
 * @file cache.h
 * @brief Thread-safe in-memory TTL cache.
 *
 * A small key/value store with per-entry expiry, useful for memoizing DB
 * lookups, rate-limiting counters, or any transient state. Complements the
 * sessions MemoryStore and ratelimit middleware.
 *
 * @code
 * using namespace socketify;
 * cache::TtlCache cache;
 * cache.set("user:42", payload, std::chrono::minutes(5));
 * if (auto v = cache.get("user:42")) { ... }
 *
 * cache.set_json("cfg", nlohmann::json{{"n", 1}}, std::chrono::seconds(30));
 * auto j = cache.get_json("cfg");   // std::optional<nlohmann::json>
 * @endcode
 */

#include <nlohmann/json.hpp>

#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace socketify::cache {

/**
 * @brief Fixed-value TTL cache keyed by string.
 *
 * All operations are mutex-guarded. Expired entries are purged lazily on
 * access; call purge_expired() to reclaim memory eagerly. The clock is
 * injectable so tests can advance time deterministically.
 */
class TtlCache {
public:
    using Clock = std::chrono::steady_clock;
    using ClockFn = std::function<Clock::time_point()>;

    /** @param default_ttl TTL applied when set() is called without one (0 = no expiry). */
    explicit TtlCache(std::chrono::milliseconds default_ttl = std::chrono::milliseconds::zero())
        : default_ttl_(default_ttl), now_(&Clock::now) {}

    /** @brief Override the time source (tests). */
    void set_clock(ClockFn fn) {
        std::lock_guard<std::mutex> lk(mu_);
        now_ = std::move(fn);
    }

    /** @brief Store @p value under @p key with @p ttl (0 => use default TTL). */
    void set(const std::string& key, std::string value,
             std::chrono::milliseconds ttl = std::chrono::milliseconds::zero());

    /** @brief Fetch @p key, or std::nullopt when missing/expired. */
    std::optional<std::string> get(const std::string& key);

    /** @brief True when @p key is present and not expired. */
    bool contains(const std::string& key);

    /** @brief Remove @p key; returns true when it existed. */
    bool erase(const std::string& key);

    /** @brief Remove all entries. */
    void clear();

    /** @brief Number of live (non-expired) entries; also purges expired ones. */
    std::size_t size();

    /** @brief Drop all expired entries now; returns the number removed. */
    std::size_t purge_expired();

    // ---- JSON convenience ----

    /** @brief Store @p value serialized as JSON. */
    void set_json(const std::string& key, const nlohmann::json& value,
                  std::chrono::milliseconds ttl = std::chrono::milliseconds::zero());

    /** @brief Fetch and parse @p key as JSON (nullopt when missing/expired/invalid). */
    std::optional<nlohmann::json> get_json(const std::string& key);

private:
    struct Entry {
        std::string value;
        bool has_expiry{false};
        Clock::time_point expires_at{};
    };

    bool expired_(const Entry& e, Clock::time_point now) const {
        return e.has_expiry && now >= e.expires_at;
    }

    std::mutex mu_;
    std::chrono::milliseconds default_ttl_;
    ClockFn now_;
    std::unordered_map<std::string, Entry> map_;
};

} // namespace socketify::cache
