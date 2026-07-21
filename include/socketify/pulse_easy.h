#pragma once
/**
 * @file pulse_easy.h
 * @brief Developer-friendly Pulse wrapper — JSON events, rooms, auto-cleanup.
 *
 * `adopt` / `bind` retain ConnectionState until the channel closes so
 * `on(...)` handlers keep working after the upgrade route returns.
 *
 * Close cleanup is composable:
 *  - `Connection::on_close` registers app callbacks (does not replace Channel handlers).
 *  - `App` always registers its own Channel close hook that runs those callbacks
 *    then `release` (idempotent).
 *  - `Channel::on_close` appends, so `pulse_media::Hub::attach` / `join` cannot
 *    wipe App release. Prefer `Connection::on_close` for app cleanup when using media.
 */

#include "socketify/pulse.h"
#include "socketify/server.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace socketify::pulse_easy {

using json = nlohmann::json;

class App;
class Connection;

struct ConnectionState {
    pulse::Channel ch;
    pulse::Hub* hub{nullptr};
    std::string default_room;
    std::unordered_map<std::string, std::function<void(Connection&, const json&)>> handlers;
    std::function<void(Connection&, std::string_view)> raw_handler;
    std::vector<std::function<void(Connection&, pulse::CloseCode, std::string_view)>> close_handlers;
};

/**
 * @brief One upgraded Pulse connection with JSON helpers.
 */
class Connection {
public:
    Connection() = default;
    Connection(std::shared_ptr<ConnectionState> state, App* app);

    pulse::Channel& channel();
    const pulse::Channel& channel() const;

    void join(std::string room);
    bool emit(std::string type, json data = json::object());
    bool broadcast(std::string_view room, std::string type, const json& data);
    void on(std::string type, std::function<void(Connection&, const json& data)> fn);
    void on_raw(std::function<void(Connection&, std::string_view raw)> fn);
    /**
     * @brief Register close cleanup. Safe with pulse_media — does not replace
     *        Channel close handlers. App still releases retained state afterward.
     */
    void on_close(std::function<void(Connection&, pulse::CloseCode, std::string_view)> fn);
    void set_default_room(std::string room);
    const std::string& default_room() const;

private:
    friend class App;
    void dispatch_text_(std::string_view raw);

    std::shared_ptr<ConnectionState> state_;
    App* app_{nullptr};
};

class App {
public:
    explicit App(pulse::Hub* shared_hub = nullptr);

    pulse::Hub& hub();
    void on(std::string path, std::function<void(Connection&)> handler,
            pulse::Options opts = {});
    void bind(Server& server);
    /**
     * @brief Adopt an upgraded channel. State is retained until close or
     *        `release`. Safe to discard the returned Connection after wiring
     *        handlers — they live in the retained state.
     */
    Connection adopt(pulse::Channel ch);
    /**
     * @brief Drop retained state for @p ch (leave rooms + free handlers).
     * Idempotent and safe to call from custom Channel close hooks.
     */
    void release(const pulse::Channel& ch);

    /** @brief True while @p ch is retained in this App (testing / diagnostics). */
    bool is_live(const pulse::Channel& ch) const;

private:
    void wire_channel_(const std::shared_ptr<ConnectionState>& state);
    void release_id_(std::uint64_t id);

    pulse::Hub owned_hub_;
    pulse::Hub* hub_;
    mutable std::mutex mu_;
    std::unordered_map<std::uint64_t, std::shared_ptr<ConnectionState>> live_;
    struct Route {
        std::string path;
        pulse::Options opts;
        std::function<void(Connection&)> handler;
    };
    std::vector<Route> routes_;
};

json envelope(std::string_view type, const json& data);
std::optional<std::pair<std::string, json>> parse_envelope(std::string_view raw);

} // namespace socketify::pulse_easy
