#pragma once
/**
 * @file pulse_easy.h
 * @brief Developer-friendly Pulse wrapper — JSON events, rooms, auto-cleanup.
 */

#include "socketify/pulse.h"
#include "socketify/server.h"

#include <nlohmann/json.hpp>

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
    Connection adopt(pulse::Channel ch);

private:
    void wire_channel_(const std::shared_ptr<ConnectionState>& state);

    pulse::Hub owned_hub_;
    pulse::Hub* hub_;
    std::mutex mu_;
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
