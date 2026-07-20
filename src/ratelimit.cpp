/**
 * @file ratelimit.cpp
 * @brief Thread-safe token-bucket rate limiter.
 */

#include "socketify/ratelimit.h"

#include <cmath>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace socketify::ratelimit {

namespace {

using Clock = std::chrono::steady_clock;

struct Bucket {
    double tokens;
    Clock::time_point last_refill;
};

struct Store {
    std::mutex mu;
    std::unordered_map<std::string, Bucket> buckets;
    Clock::time_point last_prune{Clock::now()};

    /// Take one token; returns {allowed, tokens_left, seconds_until_token}.
    struct Result {
        bool allowed;
        double remaining;
        double retry_after_s;
    };

    Result take(const std::string& key, const Options& opts) {
        auto now = Clock::now();
        std::lock_guard<std::mutex> lk(mu);

        auto [it, inserted] = buckets.try_emplace(key, Bucket{opts.capacity, now});
        Bucket& b = it->second;

        if (!inserted) {
            double elapsed = std::chrono::duration<double>(now - b.last_refill).count();
            b.tokens = std::min(opts.capacity, b.tokens + elapsed * opts.refill_per_second);
            b.last_refill = now;
        }

        Result r{};
        if (b.tokens >= 1.0) {
            b.tokens -= 1.0;
            r.allowed = true;
            r.remaining = b.tokens;
            r.retry_after_s = 0.0;
        } else {
            r.allowed = false;
            r.remaining = 0.0;
            r.retry_after_s = (opts.refill_per_second > 0.0)
                                  ? (1.0 - b.tokens) / opts.refill_per_second
                                  : 3600.0;
        }

        // Prune stale buckets occasionally (full buckets carry no state).
        if (now - last_prune > std::chrono::minutes(5)) {
            for (auto bit = buckets.begin(); bit != buckets.end();) {
                double elapsed = std::chrono::duration<double>(now - bit->second.last_refill).count();
                double t = bit->second.tokens + elapsed * opts.refill_per_second;
                if (t >= opts.capacity && bit->first != key) {
                    bit = buckets.erase(bit);
                } else {
                    ++bit;
                }
            }
            last_prune = now;
        }
        return r;
    }
};

} // namespace

Middleware middleware(Options opts) {
    auto store = std::make_shared<Store>();
    if (!opts.key_fn) {
        opts.key_fn = [](const Request& req) { return std::string(req.remote_ip()); };
    }

    return [store, opts](Request& req, Response& res, Next next) {
        std::string key = opts.key_fn(req);
        if (key.empty()) {
            next();
            return;
        }

        auto r = store->take(key, opts);

        if (opts.standard_headers) {
            res.set_header("RateLimit-Limit",
                           std::to_string(static_cast<long long>(opts.capacity)));
            res.set_header("RateLimit-Remaining",
                           std::to_string(static_cast<long long>(r.remaining)));
        }

        if (!r.allowed) {
            long long retry = static_cast<long long>(std::ceil(r.retry_after_s));
            if (retry < 1) retry = 1;
            res.set_header("Retry-After", std::to_string(retry));
            if (opts.standard_headers) {
                res.set_header("RateLimit-Reset", std::to_string(retry));
            }
            res.status(Status::TooManyRequests).send(opts.message);
            return;
        }
        next();
    };
}

} // namespace socketify::ratelimit
