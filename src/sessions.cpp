/**
 * @file sessions.cpp
 * @brief Session middleware: signed cookies + pluggable server-side store.
 */

#include "socketify/sessions.h"
#include "socketify/detail/utils.h"

#include <stdexcept>

namespace socketify::sessions {

namespace {

constexpr std::string_view kLocalKey = "socketify.session";

std::string sign_(const std::string& secret, const std::string& id) {
    auto mac = detail::hmac_sha256(secret, id);
    return detail::base64url_encode(mac.data(), mac.size());
}

/// Split "<id>.<sig>" and verify; returns the id or "" when invalid.
std::string verify_cookie_(const std::string& secret, std::string_view value) {
    auto dot = value.rfind('.');
    if (dot == std::string_view::npos || dot == 0) return {};
    std::string id(value.substr(0, dot));
    std::string sig(value.substr(dot + 1));
    std::string expect = sign_(secret, id);
    if (!detail::constant_time_equal(sig, expect)) return {};
    return id;
}

} // namespace

// ---------------------------------------------------------------------------
// MemoryStore
// ---------------------------------------------------------------------------

std::optional<nlohmann::json> MemoryStore::load(const std::string& id) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = entries_.find(id);
    if (it == entries_.end()) return std::nullopt;
    if (it->second.expires <= std::chrono::steady_clock::now()) {
        entries_.erase(it);
        return std::nullopt;
    }
    return it->second.data;
}

void MemoryStore::save(const std::string& id, const nlohmann::json& data,
                       std::chrono::seconds ttl) {
    std::lock_guard<std::mutex> lk(mu_);
    prune_locked_();
    entries_[id] = Entry{data, std::chrono::steady_clock::now() + ttl};
}

void MemoryStore::destroy(const std::string& id) {
    std::lock_guard<std::mutex> lk(mu_);
    entries_.erase(id);
}

void MemoryStore::prune_locked_() {
    if (entries_.size() < 1024) return;
    auto now = std::chrono::steady_clock::now();
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->second.expires <= now) it = entries_.erase(it);
        else ++it;
    }
}

// ---------------------------------------------------------------------------
// Middleware
// ---------------------------------------------------------------------------

Middleware middleware(Options opts) {
    if (opts.secret.empty()) {
        throw std::invalid_argument("sessions::Options::secret must not be empty");
    }
    if (!opts.store) {
        opts.store = std::make_shared<MemoryStore>();
    }
    auto shared = std::make_shared<Options>(std::move(opts));

    return [shared](Request& req, Response& res, Next next) {
        const Options& o = *shared;

        // ---- Resolve or create the session ----
        std::shared_ptr<Session> sess;
        std::string_view raw = req.cookie(o.cookie_name);
        if (!raw.empty()) {
            std::string id = verify_cookie_(o.secret, raw);
            if (!id.empty()) {
                if (auto data = o.store->load(id)) {
                    sess = std::make_shared<Session>(id, std::move(*data), false);
                }
            }
        }
        if (!sess) {
            sess = std::make_shared<Session>(detail::random_token(16),
                                             nlohmann::json::object(), true);
        }
        req.set_local(std::string(kLocalKey), sess);

        next();

        // ---- Persist and set/clear the cookie ----
        if (sess->destroyed()) {
            o.store->destroy(sess->id());
            res.set_cookie(cookies::expired(o.cookie_name, o.cookie_path));
            return;
        }

        const bool must_save = sess->dirty() || (sess->is_new() && !sess->data().empty());
        if (must_save) {
            o.store->save(sess->id(), sess->data(), o.ttl);
        }

        // Send the cookie for new sessions carrying data, and refresh it
        // for existing sessions we touched.
        if (must_save || !sess->is_new()) {
            std::string value = sess->id() + "." + sign_(o.secret, sess->id());
            Cookie c(o.cookie_name, value);
            c.path(o.cookie_path)
                .max_age(static_cast<std::int64_t>(o.ttl.count()))
                .http_only(o.cookie_http_only)
                .secure(o.cookie_secure)
                .same_site(o.same_site);
            res.set_cookie(c);
        }
    };
}

std::shared_ptr<Session> get(const Request& req) {
    return req.local<Session>(std::string(kLocalKey));
}

} // namespace socketify::sessions

