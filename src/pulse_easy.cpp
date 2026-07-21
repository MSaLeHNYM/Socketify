/**
 * @file pulse_easy.cpp
 * @brief JSON event wrapper for Pulse channels.
 */

#include "socketify/pulse_easy.h"
#include "socketify/json.h"

namespace socketify::pulse_easy {

json envelope(std::string_view type, const json& data) {
    return json{{"type", std::string(type)}, {"data", data}};
}

std::optional<std::pair<std::string, json>> parse_envelope(std::string_view raw) {
    auto doc = json::parse(raw, nullptr, false);
    if (!doc.is_object()) return std::nullopt;
    if (!doc.contains("type") || !doc["type"].is_string()) return std::nullopt;
    json data = doc.contains("data") ? doc["data"] : json::object();
    return std::pair{doc["type"].get<std::string>(), std::move(data)};
}

Connection::Connection(std::shared_ptr<ConnectionState> state, App* app)
    : state_(std::move(state)), app_(app) {}

pulse::Channel& Connection::channel() { return state_->ch; }
const pulse::Channel& Connection::channel() const { return state_->ch; }

void Connection::join(std::string room) {
    if (!state_ || !state_->ch.valid() || !state_->hub) return;
    state_->hub->join(room, state_->ch);
    state_->default_room = std::move(room);
}

bool Connection::emit(std::string type, json data) {
    if (!state_ || !state_->ch.valid()) return false;
    return state_->ch.send_text(envelope(type, data).dump());
}

bool Connection::broadcast(std::string_view room, std::string type, const json& data) {
    if (!state_ || !state_->hub) return false;
    state_->hub->broadcast_text(room, envelope(type, data).dump());
    return true;
}

void Connection::on(std::string type, std::function<void(Connection&, const json& data)> fn) {
    if (!state_) return;
    state_->handlers[std::move(type)] = std::move(fn);
}

void Connection::on_raw(std::function<void(Connection&, std::string_view raw)> fn) {
    if (!state_) return;
    state_->raw_handler = std::move(fn);
}

void Connection::on_close(
    std::function<void(Connection&, pulse::CloseCode, std::string_view)> fn) {
    if (!state_ || !fn) return;
    state_->close_handlers.push_back(std::move(fn));
}

void Connection::set_default_room(std::string room) {
    if (!state_) return;
    state_->default_room = std::move(room);
}

const std::string& Connection::default_room() const {
    static const std::string empty;
    return state_ ? state_->default_room : empty;
}

void Connection::dispatch_text_(std::string_view raw) {
    if (!state_) return;
    auto parsed = parse_envelope(raw);
    if (!parsed) {
        if (state_->raw_handler) state_->raw_handler(*this, raw);
        return;
    }
    auto it = state_->handlers.find(parsed->first);
    if (it != state_->handlers.end()) {
        it->second(*this, parsed->second);
        return;
    }
    if (state_->raw_handler) state_->raw_handler(*this, raw);
}

App::App(pulse::Hub* shared_hub) : hub_(shared_hub ? shared_hub : &owned_hub_) {}

pulse::Hub& App::hub() { return *hub_; }

void App::on(std::string path, std::function<void(Connection&)> handler, pulse::Options opts) {
    std::lock_guard<std::mutex> lk(mu_);
    routes_.push_back(Route{std::move(path), std::move(opts), std::move(handler)});
}

void App::release_id_(std::uint64_t id) {
    std::shared_ptr<ConnectionState> dropped;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = live_.find(id);
        if (it == live_.end()) return; // idempotent
        dropped = std::move(it->second);
        live_.erase(it);
    }
    if (!dropped) return;

    // Leave rooms while the Channel handle is still valid. Safe if the app
    // already called leave_all from Connection::on_close.
    if (hub_ && dropped->ch.valid()) {
        hub_->leave_all(dropped->ch);
    }

    // Drop handler tables before destroying Channel member so any nested
    // dispatch cannot observe half-destroyed maps.
    dropped->handlers.clear();
    dropped->raw_handler = nullptr;
    dropped->close_handlers.clear();
    // `dropped` destroyed at end of scope (after this close-hook frame returns
    // if the caller still holds a shared_ptr to the same state).
}

void App::release(const pulse::Channel& ch) {
    if (!ch.valid()) return;
    release_id_(ch.id());
}

bool App::is_live(const pulse::Channel& ch) const {
    if (!ch.valid()) return false;
    std::lock_guard<std::mutex> lk(mu_);
    return live_.find(ch.id()) != live_.end();
}

void App::wire_channel_(const std::shared_ptr<ConnectionState>& state) {
    std::weak_ptr<ConnectionState> weak = state;
    state->ch.on_text([weak, this](pulse::Channel&, std::string_view raw) {
        auto locked = weak.lock();
        if (!locked) return;
        Connection conn(locked, this);
        conn.dispatch_text_(raw);
    });

    // Always retain an App-owned close hook. Channel::on_close appends, so
    // pulse_media / user Channel hooks cannot wipe this release path.
    const std::uint64_t id = state->ch.id();
    state->ch.on_close([weak, this, id](pulse::Channel&, pulse::CloseCode code,
                                        std::string_view reason) {
        // Keep state alive for the full callback so user close handlers can use
        // Connection safely; release_id_ only drops the App retain.
        auto locked = weak.lock();
        if (locked) {
            auto handlers = locked->close_handlers; // copy — re-entrant safe
            Connection conn(locked, this);
            for (auto& fn : handlers) {
                if (fn) fn(conn, code, reason);
            }
        }
        release_id_(id);
    });
}

Connection App::adopt(pulse::Channel ch) {
    auto state = std::make_shared<ConnectionState>();
    state->ch = std::move(ch);
    state->hub = hub_;
    {
        std::lock_guard<std::mutex> lk(mu_);
        live_[state->ch.id()] = state;
    }
    wire_channel_(state);
    return Connection(state, this);
}

void App::bind(Server& server) {
    std::vector<Route> snap;
    {
        std::lock_guard<std::mutex> lk(mu_);
        snap = routes_;
    }
    for (const auto& route : snap) {
        server.Get(route.path, [this, route](Request& req, Response& res) {
            auto ch = pulse::upgrade(req, res, route.opts);
            if (!ch.valid()) return;
            Connection conn = adopt(std::move(ch));
            route.handler(conn);
        });
    }
}

} // namespace socketify::pulse_easy
