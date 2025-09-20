#pragma once
// socketify/server.h — main server entry, options, and glue

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "socketify/http.h"
#include "socketify/request.h"
#include "socketify/response.h"
#include "socketify/router.h"
#include "socketify/middleware.h"
#include "socketify/compression.h"
#include "socketify/cors.h"
#include "socketify/static_files.h"

namespace socketify {

// ---------------------------
// Options
// ---------------------------
struct ServerOptions {
    // timeouts (milliseconds)
    std::chrono::milliseconds header_timeout{15000};
    std::chrono::milliseconds body_timeout{30000};
    std::chrono::milliseconds idle_timeout{60000};

    // socket options
    int backlog{128};
    bool reuse_port{false};
    bool reuse_addr{true};

    // worker/acceptor
    unsigned workers{0};   // 0 -> hardware_concurrency
    unsigned acceptors{1};

    // compression
    compression::Options compression{};

    // future TLS options could go here (cert_dir, etc.)
};

// ---------------------------
// Server
// ---------------------------
class Server {
public:
    explicit Server(ServerOptions opts = {});
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Routing
    Route& AddRoute(Method m, std::string_view path, Handler h);
    Router::RouteGroup Group(std::string_view prefix) { return router_.Group(prefix); }
    Server& Use(Middleware mw) { router_.Use(std::move(mw)); return *this; }

    // Start/Stop
    bool Run(std::string_view ip, uint16_t port);
    void Stop();

private:
    // I/O
    int listen_fd_{-1};
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{false};

    // state
    ServerOptions opts_;
    Router router_;

    // accept + handle
    void accept_loop_();
    void handle_connection_(int client_fd);

    // response serialization
    static std::string serialize_response_(const Request& req,
                                           const Response& res,
                                           const ServerOptions& opts);

    static bool should_close_(const Request& req, const Response& res);

    // small helpers
    static std::string make_date_header_();
};

} // namespace socketify
