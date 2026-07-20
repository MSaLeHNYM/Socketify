#pragma once
/**
 * @file sessions.h
 * @brief Cookie-based sessions with HMAC-SHA256 signed session ids.
 *
 * The session id cookie is signed (`<id>.<base64url hmac>`), so it cannot
 * be forged without the secret. Session data lives server-side in a Store
 * (in-memory by default; implement Store for Redis etc.).
 *
 * @code
 * sessions::Options so;
 * so.secret = "change-me-please-32-bytes-min";
 * server.Use(sessions::middleware(so));
 *
 * server.Post("/login", [](Request& req, Response& res) {
 *     auto sess = sessions::get(req);
 *     sess->set("user", "alice");
 *     res.send("logged in\n");
 * });
 *
 * server.Get("/me", [](Request& req, Response& res) {
 *     auto sess = sessions::get(req);
 *     if (!sess->has("user")) { res.status(Status::Unauthorized).send("who?\n"); return; }
 *     res.json({{"user", sess->get("user")}});
 * });
 * @endcode
 */

#include "socketify/cookies.h"
#include "socketify/middleware.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace socketify::sessions {

/**
 * @brief Per-request session object (attached to the Request by the
 *        middleware; retrieve it with sessions::get()).
 */
class Session {
public:
    /** @brief Construct (middleware-internal). */
    Session(std::string id, nlohmann::json data, bool is_new)
        : id_(std::move(id)), data_(std::move(data)), new_(is_new) {
        if (!data_.is_object()) data_ = nlohmann::json::object();
    }

    /** @brief Session id (unsigned part of the cookie). */
    const std::string& id() const noexcept { return id_; }
    /** @brief True when the session was created for this request. */
    bool is_new() const noexcept { return new_; }

    /** @brief True when @p key exists. */
    bool has(const std::string& key) const { return data_.contains(key); }

    /** @brief Value at @p key (null json when absent). */
    nlohmann::json get(const std::string& key) const {
        auto it = data_.find(key);
        return it == data_.end() ? nlohmann::json{} : *it;
    }

    /** @brief Set @p key to @p value (marks the session dirty). */
    void set(const std::string& key, nlohmann::json value) {
        data_[key] = std::move(value);
        dirty_ = true;
    }

    /** @brief Remove @p key. */
    void erase(const std::string& key) {
        if (data_.erase(key) > 0) dirty_ = true;
    }

    /** @brief Remove all keys. */
    void clear() {
        if (!data_.empty()) dirty_ = true;
        data_ = nlohmann::json::object();
    }

    /** @brief Destroy the session: server data is deleted and the cookie
     *         expired when the response is sent. */
    void destroy() noexcept { destroyed_ = true; }

    /** @brief Raw data object. */
    const nlohmann::json& data() const noexcept { return data_; }

    /// @cond INTERNAL
    bool dirty() const noexcept { return dirty_; }
    bool destroyed() const noexcept { return destroyed_; }
    /// @endcond

private:
    std::string id_;
    nlohmann::json data_;
    bool new_{false};
    bool dirty_{false};
    bool destroyed_{false};
};

/**
 * @brief Server-side session storage interface.
 *
 * Implementations must be thread-safe; the middleware calls these from
 * multiple worker threads.
 */
class Store {
public:
    virtual ~Store() = default;
    /** @brief Load session data; std::nullopt when missing/expired. */
    virtual std::optional<nlohmann::json> load(const std::string& id) = 0;
    /** @brief Persist @p data under @p id for @p ttl. */
    virtual void save(const std::string& id, const nlohmann::json& data,
                      std::chrono::seconds ttl) = 0;
    /** @brief Delete the session. */
    virtual void destroy(const std::string& id) = 0;
};

/** @brief Built-in thread-safe in-memory store with TTL expiry. */
class MemoryStore : public Store {
public:
    std::optional<nlohmann::json> load(const std::string& id) override;
    void save(const std::string& id, const nlohmann::json& data,
              std::chrono::seconds ttl) override;
    void destroy(const std::string& id) override;

private:
    struct Entry {
        nlohmann::json data;
        std::chrono::steady_clock::time_point expires;
    };
    std::mutex mu_;
    std::unordered_map<std::string, Entry> entries_;
    void prune_locked_();
};

/** @brief Session middleware configuration. */
struct Options {
    /** @brief HMAC secret. REQUIRED; use >= 32 random bytes in production. */
    std::string secret;
    /** @brief Cookie name (default "sid"). */
    std::string cookie_name{"sid"};
    /** @brief Session lifetime; also used as cookie Max-Age. */
    std::chrono::seconds ttl{std::chrono::hours(24)};
    /** @brief Storage backend; defaults to a shared MemoryStore. */
    std::shared_ptr<Store> store{};
    /** @brief Set the Secure cookie attribute (enable behind HTTPS). */
    bool cookie_secure{false};
    /** @brief Set the HttpOnly attribute (default on). */
    bool cookie_http_only{true};
    /** @brief SameSite attribute (default Lax). */
    SameSite same_site{SameSite::Lax};
    /** @brief Cookie Path attribute. */
    std::string cookie_path{"/"};
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
