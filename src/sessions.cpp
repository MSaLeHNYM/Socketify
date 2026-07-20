/**
 * @file sessions.cpp
 * @brief Session manager: ServerStore, SignedCookie, and JWT strategies.
 */

#include "socketify/sessions.h"
#include "socketify/detail/utils.h"

#include <stdexcept>

namespace socketify::sessions {

namespace {

constexpr std::string_view kLocalKey = "socketify.session";

std::string hmac_b64_(std::string_view secret, std::string_view msg) {
    auto mac = detail::hmac_sha256(secret, msg);
    return detail::base64url_encode(mac.data(), mac.size());
}

std::string sign_id_(const std::string& secret, const std::string& id) {
    return hmac_b64_(secret, id);
}

std::string verify_id_cookie_(const std::string& secret, std::string_view value) {
    auto dot = value.rfind('.');
    if (dot == std::string_view::npos || dot == 0) return {};
    std::string id(value.substr(0, dot));
    std::string sig(value.substr(dot + 1));
    if (!detail::constant_time_equal(sig, sign_id_(secret, id))) return {};
    return id;
}

std::string seal_payload_(const std::string& secret, const nlohmann::json& data) {
    std::string payload = data.dump();
    auto body = detail::base64url_encode(
        reinterpret_cast<const unsigned char*>(payload.data()), payload.size());
    return body + "." + hmac_b64_(secret, body);
}

std::optional<nlohmann::json> unseal_payload_(const std::string& secret,
                                              std::string_view value) {
    auto dot = value.rfind('.');
    if (dot == std::string_view::npos || dot == 0) return std::nullopt;
    std::string_view body = value.substr(0, dot);
    std::string_view sig = value.substr(dot + 1);
    if (!detail::constant_time_equal(sig, hmac_b64_(secret, body))) return std::nullopt;
    auto raw = detail::base64url_decode(body);
    if (!raw) return std::nullopt;
    try {
        auto j = nlohmann::json::parse(*raw);
        if (!j.is_object()) return std::nullopt;
        return j;
    } catch (...) {
        return std::nullopt;
    }
}

std::string_view bearer_token_(const Request& req) {
    auto auth = req.header("Authorization");
    if (auth.size() > 7 && detail::iequal_ascii(auth.substr(0, 7), "Bearer ")) {
        return detail::trim_view(auth.substr(7));
    }
    // also accept lowercase "bearer " already handled by iequal on first 7
    if (auth.size() > 7 && detail::istarts_with(auth, "bearer ")) {
        return detail::trim_view(auth.substr(7));
    }
    return {};
}

void set_session_cookie_(Response& res, const Options& o, const std::string& value) {
    Cookie c(o.cookie_name, value);
    c.path(o.cookie_path)
        .max_age(static_cast<std::int64_t>(o.ttl.count()))
        .http_only(o.cookie_http_only)
        .secure(o.cookie_secure)
        .same_site(o.same_site);
    res.set_cookie(c);
}

void clear_session_cookie_(Response& res, const Options& o) {
    res.set_cookie(cookies::expired(o.cookie_name, o.cookie_path));
}

std::string new_id_() { return detail::random_token(16); }

} // namespace

void Session::regenerate() {
    regenerated_ = true;
    dirty_ = true;
    touched_ = true;
    new_ = true;
}

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

std::size_t MemoryStore::size() {
    std::lock_guard<std::mutex> lk(mu_);
    prune_locked_();
    auto now = std::chrono::steady_clock::now();
    std::size_t n = 0;
    for (auto& [_, e] : entries_) {
        if (e.expires > now) ++n;
    }
    return n;
}

void MemoryStore::prune_locked_() {
    if (entries_.size() < 256) return;
    auto now = std::chrono::steady_clock::now();
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->second.expires <= now) it = entries_.erase(it);
        else ++it;
    }
}

// ---------------------------------------------------------------------------
// JWT helpers
// ---------------------------------------------------------------------------

namespace jwt {

namespace {

std::string b64_json_(const nlohmann::json& j) {
    std::string s = j.dump();
    return detail::base64url_encode(
        reinterpret_cast<const unsigned char*>(s.data()), s.size());
}

} // namespace

std::string encode(std::string_view secret, nlohmann::json claims,
                   std::chrono::seconds ttl) {
    using clock = std::chrono::system_clock;
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         clock::now().time_since_epoch())
                         .count();
    if (!claims.contains("iat")) claims["iat"] = now;
    if (!claims.contains("exp")) claims["exp"] = now + ttl.count();

    nlohmann::json header = {{"alg", "HS256"}, {"typ", "JWT"}};
    std::string h = b64_json_(header);
    std::string p = b64_json_(claims);
    std::string signing_input = h + "." + p;
    return signing_input + "." + hmac_b64_(secret, signing_input);
}

std::optional<nlohmann::json> decode(std::string_view secret, std::string_view token) {
    auto d1 = token.find('.');
    if (d1 == std::string_view::npos) return std::nullopt;
    auto d2 = token.find('.', d1 + 1);
    if (d2 == std::string_view::npos) return std::nullopt;

    std::string_view h = token.substr(0, d1);
    std::string_view p = token.substr(d1 + 1, d2 - d1 - 1);
    std::string_view s = token.substr(d2 + 1);
    if (h.empty() || p.empty() || s.empty()) return std::nullopt;

    std::string signing_input = std::string(h) + "." + std::string(p);
    if (!detail::constant_time_equal(s, hmac_b64_(secret, signing_input))) {
        return std::nullopt;
    }

    auto raw = detail::base64url_decode(p);
    if (!raw) return std::nullopt;
    nlohmann::json claims;
    try {
        claims = nlohmann::json::parse(*raw);
    } catch (...) {
        return std::nullopt;
    }
    if (!claims.is_object()) return std::nullopt;

    if (claims.contains("exp")) {
        try {
            auto exp = claims["exp"].get<std::int64_t>();
            auto now = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
            if (now >= exp) return std::nullopt;
        } catch (...) {
            return std::nullopt;
        }
    }
    return claims;
}

} // namespace jwt

// ---------------------------------------------------------------------------
// Middleware
// ---------------------------------------------------------------------------

Middleware middleware(Options opts) {
    if (opts.secret.empty()) {
        throw std::invalid_argument("sessions::Options::secret must not be empty");
    }
    if (opts.strategy == Strategy::ServerStore && !opts.store) {
        opts.store = std::make_shared<MemoryStore>();
    }
    auto shared = std::make_shared<Options>(std::move(opts));

    return [shared](Request& req, Response& res, Next next) {
        const Options& o = *shared;
        std::shared_ptr<Session> sess;

        // ---- Load ----
        switch (o.strategy) {
            case Strategy::ServerStore: {
                std::string_view raw = req.cookie(o.cookie_name);
                if (!raw.empty()) {
                    std::string id = verify_id_cookie_(o.secret, raw);
                    if (!id.empty()) {
                        if (auto data = o.store->load(id)) {
                            sess = std::make_shared<Session>(std::move(id), std::move(*data),
                                                             false);
                        }
                    }
                }
                break;
            }
            case Strategy::SignedCookie: {
                std::string_view raw = req.cookie(o.cookie_name);
                if (!raw.empty()) {
                    if (auto data = unseal_payload_(o.secret, raw)) {
                        // id is a fingerprint of the sealed blob for regenerate tracking
                        sess = std::make_shared<Session>(new_id_(), std::move(*data), false);
                    }
                }
                break;
            }
            case Strategy::JWT: {
                std::string_view token;
                if (o.jwt_transport == JwtTransport::Bearer ||
                    o.jwt_transport == JwtTransport::Both) {
                    token = bearer_token_(req);
                }
                if (token.empty() &&
                    (o.jwt_transport == JwtTransport::Cookie ||
                     o.jwt_transport == JwtTransport::Both)) {
                    token = req.cookie(o.cookie_name);
                }
                if (!token.empty()) {
                    if (auto claims = jwt::decode(o.secret, token)) {
                        nlohmann::json data = nlohmann::json::object();
                        if (claims->contains(o.jwt_data_claim) &&
                            (*claims)[o.jwt_data_claim].is_object()) {
                            data = (*claims)[o.jwt_data_claim];
                        } else {
                            // Flat claims: copy non-standard keys into session data
                            for (auto it = claims->begin(); it != claims->end(); ++it) {
                                if (it.key() == "iat" || it.key() == "exp" ||
                                    it.key() == "iss" || it.key() == "nbf" ||
                                    it.key() == "jti")
                                    continue;
                                data[it.key()] = it.value();
                            }
                        }
                        std::string id;
                        if (claims->contains("jti") && (*claims)["jti"].is_string()) {
                            id = (*claims)["jti"].get<std::string>();
                        } else {
                            id = new_id_();
                        }
                        sess = std::make_shared<Session>(std::move(id), std::move(data), false);
                    }
                }
                break;
            }
        }

        if (!sess) {
            sess = std::make_shared<Session>(new_id_(), nlohmann::json::object(), true);
        }
        req.set_local(std::string(kLocalKey), sess);

        next();

        // ---- Persist / clear ----
        if (sess->destroyed()) {
            if (o.strategy == Strategy::ServerStore && o.store) {
                o.store->destroy(sess->id());
            }
            if (o.strategy != Strategy::JWT ||
                o.jwt_transport != JwtTransport::Bearer) {
                clear_session_cookie_(res, o);
            }
            // Best-effort: also clear cookie when Both
            if (o.strategy == Strategy::JWT && o.jwt_transport == JwtTransport::Both) {
                clear_session_cookie_(res, o);
            }
            return;
        }

        if (sess->regenerated() && o.strategy == Strategy::ServerStore) {
            // Drop previous id if we still know it was replaced mid-request.
            // regenerate() keeps the same object; assign a fresh id now.
            std::string old = sess->id();
            sess->set_id_(new_id_());
            if (o.store && !old.empty()) o.store->destroy(old);
        }

        const bool has_data = !sess->data().empty();
        const bool should_persist =
            sess->dirty() || sess->regenerated() ||
            (o.rolling && !sess->is_new()) ||
            (o.save_uninitialized && sess->is_new()) ||
            (sess->is_new() && has_data) ||
            sess->touched();

        if (!should_persist) return;

        // Brand-new empty sessions: skip unless save_uninitialized.
        if (sess->is_new() && !has_data && !o.save_uninitialized && !sess->regenerated()) {
            return;
        }

        switch (o.strategy) {
            case Strategy::ServerStore: {
                o.store->save(sess->id(), sess->data(), o.ttl);
                std::string value = sess->id() + "." + sign_id_(o.secret, sess->id());
                set_session_cookie_(res, o, value);
                break;
            }
            case Strategy::SignedCookie: {
                set_session_cookie_(res, o, seal_payload_(o.secret, sess->data()));
                break;
            }
            case Strategy::JWT: {
                nlohmann::json claims = nlohmann::json::object();
                claims["jti"] = sess->id();
                claims[o.jwt_data_claim] = sess->data();
                if (!o.jwt_issuer.empty()) claims["iss"] = o.jwt_issuer;
                std::string token = jwt::encode(o.secret, std::move(claims), o.ttl);

                if (o.jwt_transport == JwtTransport::Cookie ||
                    o.jwt_transport == JwtTransport::Both) {
                    set_session_cookie_(res, o, token);
                }
                if (o.jwt_transport == JwtTransport::Bearer ||
                    o.jwt_transport == JwtTransport::Both) {
                    // Expose token so handlers can return it in JSON bodies.
                    res.set_header("X-Access-Token", token);
                }
                break;
            }
        }
    };
}

std::shared_ptr<Session> get(const Request& req) {
    return req.local<Session>(std::string(kLocalKey));
}

} // namespace socketify::sessions
