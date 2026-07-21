/**
 * @file cache.cpp
 * @brief Thread-safe TTL cache implementation.
 */

#include "socketify/cache.h"

namespace socketify::cache {

void TtlCache::set(const std::string& key, std::string value, std::chrono::milliseconds ttl) {
    std::lock_guard<std::mutex> lk(mu_);
    Entry e;
    e.value = std::move(value);
    std::chrono::milliseconds effective = ttl.count() > 0 ? ttl : default_ttl_;
    if (effective.count() > 0) {
        e.has_expiry = true;
        e.expires_at = now_() + effective;
    }
    map_[key] = std::move(e);
}

std::optional<std::string> TtlCache::get(const std::string& key) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = map_.find(key);
    if (it == map_.end()) return std::nullopt;
    if (expired_(it->second, now_())) {
        map_.erase(it);
        return std::nullopt;
    }
    return it->second.value;
}

bool TtlCache::contains(const std::string& key) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = map_.find(key);
    if (it == map_.end()) return false;
    if (expired_(it->second, now_())) {
        map_.erase(it);
        return false;
    }
    return true;
}

bool TtlCache::erase(const std::string& key) {
    std::lock_guard<std::mutex> lk(mu_);
    return map_.erase(key) > 0;
}

void TtlCache::clear() {
    std::lock_guard<std::mutex> lk(mu_);
    map_.clear();
}

std::size_t TtlCache::size() {
    purge_expired();
    std::lock_guard<std::mutex> lk(mu_);
    return map_.size();
}

std::size_t TtlCache::purge_expired() {
    std::lock_guard<std::mutex> lk(mu_);
    auto now = now_();
    std::size_t removed = 0;
    for (auto it = map_.begin(); it != map_.end();) {
        if (expired_(it->second, now)) {
            it = map_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

void TtlCache::set_json(const std::string& key, const nlohmann::json& value,
                        std::chrono::milliseconds ttl) {
    set(key, value.dump(), ttl);
}

std::optional<nlohmann::json> TtlCache::get_json(const std::string& key) {
    auto s = get(key);
    if (!s) return std::nullopt;
    auto j = nlohmann::json::parse(*s, /*cb=*/nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) return std::nullopt;
    return j;
}

} // namespace socketify::cache
