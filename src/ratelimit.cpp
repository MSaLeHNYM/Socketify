#include "socketify/ratelimit.h"
#include "socketify/request.h"
#include "socketify/response.h"

#include <deque>
#include <mutex>
#include <unordered_map>

namespace socketify {

namespace rate_limit {

// Internal state for the rate limiter
class LimiterState {
public:
    explicit LimiterState(Options opts) : opts_(std::move(opts)) {}

    bool check(const std::string& ip) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto& timestamps = client_hits_[ip];
        auto now = std::chrono::steady_clock::now();

        // Remove old timestamps
        auto window_start = now - opts_.window;
        while (!timestamps.empty() && timestamps.front() < window_start) {
            timestamps.pop_front();
        }

        // Check if limit is exceeded
        if (timestamps.size() >= static_cast<size_t>(opts_.max_requests)) {
            return false; // rate limited
        }

        // Record new hit
        timestamps.push_back(now);
        return true; // ok
    }

    const Options& options() const { return opts_; }

private:
    using TimestampQueue = std::deque<std::chrono::steady_clock::time_point>;

    Options opts_;
    std::mutex mutex_;
    std::unordered_map<std::string, TimestampQueue> client_hits_;
};

Middleware Create(Options opts) {
    auto state = std::make_shared<LimiterState>(std::move(opts));

    return [state](Request& req, Response& res, Next next) {
        // Determine client IP
        std::string client_ip;
        const auto& ip_header = state->options().ip_header;
        if (!ip_header.empty()) {
            client_ip = std::string(req.header(ip_header));
        }
        if (client_ip.empty()) {
            // Fallback to connection address (needs to be plumbed into Request)
            // For now, let's assume a placeholder is available.
            // In a real implementation, `req` would need `get_remote_address()`.
            // Let's use a placeholder attribute for now.
            auto it = req.params().find("__remote_addr");
            if (it != req.params().end()) {
                 client_ip = it->second;
            } else {
                 client_ip = "127.0.0.1"; // Default if not found
            }
        }

        // Check rate limit
        if (!state->check(client_ip)) {
            const auto& rl_opts = state->options();
            res.status(rl_opts.status_code)
               .set_header("Retry-After", std::to_string(std::chrono::seconds(rl_opts.window).count()))
               .send(rl_opts.message);
            // Do not call next()
        } else {
            next();
        }
    };
}

} // namespace rate_limit

} // namespace socketify