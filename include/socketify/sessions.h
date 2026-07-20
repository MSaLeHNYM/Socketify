#pragma once
/**
 * @file sessions.h
 * @brief Fast, pluggable session manager.
 *
 * Pick a strategy via Options::strategy:
 *
 *  - **ServerStore** (default): HMAC-signed session-id cookie + server Store
 *    (MemoryStore by default; plug in Redis/etc.).
 *  - **SignedCookie**: entire session JSON sealed in a signed cookie (stateless).
 *  - **JWT**: HS256 JWT carried in a cookie and/or `Authorization: Bearer`.
 *
 * @code
 * sessions::Options so;
 * so.secret = "change-me-please-32-bytes-min";
 * so.strategy = sessions::Strategy::ServerStore; // or SignedCookie / JWT
 * so.rolling = true;                            // extend TTL on each request
 * server.Use(sessions::middleware(so));
 *
 * server.Post("/login", [](Request& req, Response& res) {
 *     auto sess = sessions::get(req);
 *     sess->regenerate();          // session-fixation protection
 *     sess->set("user", "alice");
 *     res.send("ok\n");
 * });
 * @endcode
 */

#include "socketify/cookies.h"
#include "socketify/middleware.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace socketify::sessions {

/** @brief How session state is carried and verified. */
enum class Strategy : std::uint8_t {
    /** Signed `<id>.<hmac>` cookie; data lives in Store. */
    ServerStore = 0,
    /** Signed cookie containing the JSON payload (no server store). */
    SignedCookie = 1,
    /** HS256 JWT in cookie and/or Authorization Bearer header. */
    JWT = 2,
};

/** @brief Where to read/write JWTs when Strategy::JWT is selected. */
enum class JwtTransport : std::uint8_t {
    Cookie = 0,   ///< Cookie only
    Bearer = 1,   ///< Authorization: Bearer only
    Both = 2,     ///< Prefer Bearer, fall back to cookie; write both when saving
};

/**
 * @brief Per-request session object (attached by the middleware;
 *        retrieve with sessions::get()).
 */
class Session {
public:
    Session(std::string id, nlohmann::json data, bool is_new)
        : id_(std::move(id)), data_(std::move(data)), new_(is_new) {
        if (!data_.is_object()) data_ = nlohmann::json::object();
    }

    const std::string& id() const noexcept { return id_; }
    bool is_new() const noexcept { return new_; }

    bool has(const std::string& key) const { return data_.contains(key); }

    nlohmann::json get(const std::string& key) const {
        auto it = data_.find(key);
        return it == data_.end() ? nlohmann::json{} : *it;
    }

    void set(const std::string& key, nlohmann::json value) {
        data_[key] = std::move(value);
        dirty_ = true;
    }

    void erase(const std::string& key) {
        if (data_.erase(key) > 0) dirty_ = true;
    }

    void clear() {
        if (!data_.empty()) dirty_ = true;
        data_ = nlohmann::json::object();
    }

    /** @brief Destroy: wipe server data / expire cookie / invalidate JWT. */
    void destroy() noexcept { destroyed_ = true; }

    /**
     * @brief Issue a fresh session id (ServerStore) or force re-seal.
     *        Call on login to prevent session fixation.
     */
    void regenerate();

    /** @brief Mark the session so TTL/cookie is refreshed even if data unchanged. */
    void touch() noexcept { touched_ = true; }

    const nlohmann::json& data() const noexcept { return data_; }

    /// @cond INTERNAL
    bool dirty() const noexcept { return dirty_; }
    bool touched() const noexcept { return touched_; }
    bool destroyed() const noexcept { return destroyed_; }
    bool regenerated() const noexcept { return regenerated_; }
    void set_id_(std::string id) { id_ = std::move(id); }
    /// @endcond

private:
    std::string id_;
    nlohmann::json data_;
    bool new_{false};
    bool dirty_{false};
    bool touched_{false};
    bool destroyed_{false};
    bool regenerated_{false};
};

/**
 * @brief Server-side session storage interface (Strategy::ServerStore).
 *        Implementations must be thread-safe.
 */
class Store {
public:
    virtual ~Store() = default;
    virtual std::optional<nlohmann::json> load(const std::string& id) = 0;
    virtual void save(const std::string& id, const nlohmann::json& data,
                      std::chrono::seconds ttl) = 0;
    virtual void destroy(const std::string& id) = 0;
};

/** @brief Thread-safe in-memory store with TTL expiry. */
class MemoryStore : public Store {
public:
    std::optional<nlohmann::json> load(const std::string& id) override;
    void save(const std::string& id, const nlohmann::json& data,
              std::chrono::seconds ttl) override;
    void destroy(const std::string& id) override;

    /** @brief Number of live (non-expired) entries — useful for tests. */
    std::size_t size();

private:
    struct Entry {
        nlohmann::json data;
        std::chrono::steady_clock::time_point expires;
    };
    std::mutex mu_;
    std::unordered_map<std::string, Entry> entries_;
    void prune_locked_();
};

/** @brief Low-level HS256 JWT helpers (also used by Strategy::JWT). */
namespace jwt {
/** @brief Encode @p claims as an HS256 JWT. Adds iat/exp when missing. */
std::string encode(std::string_view secret, nlohmann::json claims,
                   std::chrono::seconds ttl = std::chrono::hours(24));

/** @brief Verify and decode; nullopt on bad signature / expiry / parse error. */
std::optional<nlohmann::json> decode(std::string_view secret, std::string_view token);
} // namespace jwt

/** @brief Session middleware configuration. */
struct Options {
    /** @brief HMAC / JWT secret. REQUIRED; use ≥ 32 random bytes in production. */
    std::string secret;

    /** @brief Persistence / transport strategy (default ServerStore). */
    Strategy strategy{Strategy::ServerStore};

    /** @brief Cookie name (default "sid"). */
    std::string cookie_name{"sid"};

    /** @brief Lifetime for store TTL, cookie Max-Age, and JWT exp. */
    std::chrono::seconds ttl{std::chrono::hours(24)};

    /**
     * @brief When true (default), each request that loads an existing session
     *        extends server TTL and refreshes the cookie/JWT expiry.
     */
    bool rolling{true};

    /**
     * @brief When false (default), empty brand-new sessions do not set a cookie.
     *        Set true only if you need a sid before any data is written.
     */
    bool save_uninitialized{false};

    /** @brief Storage backend for ServerStore; defaults to a shared MemoryStore. */
    std::shared_ptr<Store> store{};

    /** @brief Secure cookie attribute (enable behind HTTPS). */
    bool cookie_secure{false};
    /** @brief HttpOnly attribute (default on). */
    bool cookie_http_only{true};
    /** @brief SameSite attribute (default Lax). */
    SameSite same_site{SameSite::Lax};
    /** @brief Cookie Path attribute. */
    std::string cookie_path{"/"};

    // ---- JWT-specific ----
    JwtTransport jwt_transport{JwtTransport::Both};
    /** @brief Optional JWT iss claim. */
    std::string jwt_issuer{};
    /** @brief Claim name that holds session key/values (default "data"). */
    std::string jwt_data_claim{"data"};
};

/**
 * @brief Create the session middleware.
 * @throws std::invalid_argument when Options::secret is empty.
 */
Middleware middleware(Options opts);

/**
 * @brief Fetch the Session attached to @p req by the middleware.
 * @return The session, or nullptr when the middleware did not run.
 */
std::shared_ptr<Session> get(const Request& req);

} // namespace socketify::sessions
