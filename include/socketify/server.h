#pragma once
// socketify/server.h — HTTP(S) server (v1, blocking accept + per-conn threads)

#include "socketify/http.h"
#include "socketify/router.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace socketify {

// --- TLS options (placeholder for v1; wiring later) ---
struct TLSOptions {
    std::string cert_dir;
    std::string cert_file;
    std::string key_file;
    std::string ca_file;
    std::vector<std::string> alpn;
};

// --- Server options ---
struct ServerOptions {
    bool enable_https{false};
    TLSOptions tls{};

    std::size_t max_header_bytes{64 * 1024};
    std::size_t max_body_bytes{10 * 1024 * 1024};

    int backlog{256};

    int acceptor_threads{1}; // unused in v1 (single acceptor)
    int worker_threads{0};   // unused in v1 (thread-per-conn)

    // timeouts (ms)
    int read_header_timeout_ms{15000};
    int read_body_timeout_ms{60000};
    int idle_timeout_ms{60000};

    bool enable_request_logging{true};
};

// --- Server class ---
class Server {
public:
    explicit Server(ServerOptions opts = {});
    ~Server();

    // Global middleware
    Server& Use(Middleware mw) { router_.Use(std::move(mw)); return *this; }

    // Routes
    Route& AddRoute(Method m, std::string_view path, Handler h) {
        return router_.AddRoute(m, path, std::move(h));
    }

    // Groups
    Router::RouteGroup Group(std::string_view prefix) { return router_.Group(prefix); }

    // Run/Stop
    bool Run(std::string_view ip, uint16_t port);
    void Stop();

    // Errors
    using ErrorHandler = std::function<void(const std::string&)>;
    void OnError(ErrorHandler h) { on_error_ = std::move(h); }

    // Access
    const ServerOptions& options() const noexcept { return opts_; }
    Router& router() noexcept { return router_; }
    const Router& router() const noexcept { return router_; }

private:
    ServerOptions opts_;
    Router        router_;

    int              listen_fd_{-1};
    std::atomic<bool> running_{false};
    std::thread      accept_thread_{};

    ErrorHandler  on_error_{};

    // acceptor
    void accept_loop_();
    static int  create_listen_socket_(std::string_view ip, uint16_t port, int backlog, std::string& err);

    // per-connection handling
    void handle_connection_(int client_fd);
    static bool write_all_(int fd, const void* buf, size_t len);

    // HTTP serialization
    static std::string serialize_response_(const Response& res);
    static std::string make_date_header_(); // RFC7231 IMF-fixdate
    static bool should_close_(const Request& req, const Response& res);
};

} // namespace socketify
