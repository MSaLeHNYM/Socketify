#include "socketify/sessions.h"
#include "socketify/request.h"
#include "socketify/response.h"
#include "socketify/detail/utils.h" // for crypto placeholders

#include <chrono>
#include <map>
#include <mutex>

namespace socketify {

// Session implementation
std::string_view Session::get(std::string_view key) const {
    auto it = data_.find(std::string(key));
    if (it != data_.end()) {
        return it->second;
    }
    return {};
}

void Session::set(std::string key, std::string value) {
    data_[std::move(key)] = std::move(value);
}

void Session::unset(std::string_view key) {
    data_.erase(std::string(key));
}

void Session::destroy() {
    destroyed_ = true;
    data_.clear();
}


namespace sessions {

// Internal session store
class SessionStore {
public:
    std::shared_ptr<Session> get(const std::string& sid) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(sid);
        if (it != sessions_.end()) {
            return it->second;
        }
        return nullptr;
    }

    void set(const std::string& sid, std::shared_ptr<Session> session) {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_[sid] = std::move(session);
    }

    void destroy(const std::string& sid) {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_.erase(sid);
    }

private:
    std::mutex mutex_;
    std::map<std::string, std::shared_ptr<Session>> sessions_;
};


// Simple cookie signing placeholder.
// In a real implementation, use HMAC-SHA256.
std::string sign_cookie(std::string_view value, std::string_view secret) {
    // WARNING: This is NOT a secure signature. For demonstration only.
    return std::string(value) + "." + detail::hmac_sha256_placeholder(value, secret);
}

std::optional<std::string> unsign_cookie(std::string_view signed_value, std::string_view secret) {
    auto pos = signed_value.rfind('.');
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }
    auto value = signed_value.substr(0, pos);
    auto sig = signed_value.substr(pos + 1);
    if (sig == detail::hmac_sha256_placeholder(value, secret)) {
        return std::string(value);
    }
    return std::nullopt;
}


Middleware Create(Options opts) {
    auto store = std::make_shared<SessionStore>();
    auto options = std::make_shared<Options>(std::move(opts));

    return [store, options](Request& req, Response& res, Next next) {
        // 1. Find session ID from cookie
        std::string sid;
        auto cookie_val = req.cookie(options->cookie_name);
        if (!cookie_val.empty()) {
            auto unsigned_sid = unsign_cookie(cookie_val, options->secret);
            if (unsigned_sid) {
                sid = *unsigned_sid;
            }
        }

        // 2. Load or create session
        std::shared_ptr<Session> session;
        if (!sid.empty()) {
            session = store->get(sid);
        }
        if (!session) {
            sid = detail::generate_random_string(32); // new session
            session = std::make_shared<Session>();
        }

        // 3. Attach session to request context (placeholder)
        // req.set_context("session", session);

        next();

        // 4. On response finish, save session and set cookie
        // This requires a response "on_finish" hook.
        // For now, we'll just assume we can modify the response directly.

        // Placeholder for post-handler logic:
        // if (session->is_destroyed()) {
        //     store->destroy(sid);
        //     res.set_cookie(... expires=now ...);
        // } else if (!session->empty()) {
        //     store->set(sid, session);
        //     res.set_cookie(sign_cookie(sid, options->secret), ...);
        // }
    };
}

} // namespace sessions

} // namespace socketify