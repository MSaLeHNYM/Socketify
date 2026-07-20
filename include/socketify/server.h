#pragma once
/**
 * @file server.h
 * @brief Main server entry point: options, lifecycle and routing sugar.
 *
 * @code
 * #include <socketify/socketify.h>
 * using namespace socketify;
 *
 * int main() {
 *     Server server;
 *     server.Get("/", [](Request&, Response& res) {
 *         res.send("Hello, world!\n");
 *     });
 *     server.Run("0.0.0.0", 8080);
 *     server.Wait();
 * }
 * @endcode
 *
 * Architecture: N worker threads (default = hardware cores), each running
 * its own epoll event loop with a SO_REUSEPORT listener, so the kernel
 * load-balances connections with no accept contention. Handlers run
 * synchronously on the loop that owns the connection.
 */

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

#include "socketify/compression.h"
#include "socketify/http.h"
#include "socketify/middleware.h"
#include "socketify/request.h"
#include "socketify/response.h"
#include "socketify/router.h"
#include "socketify/tls.h"

namespace socketify {

namespace detail { class Worker; }

/** @brief Server configuration. All fields have sensible defaults. */
struct ServerOptions {
    /** @brief Max time to receive a full header section. */
    std::chrono::milliseconds header_timeout{15000};
    /** @brief Max time to receive the request body. */
    std::chrono::milliseconds body_timeout{30000};
    /** @brief Max keep-alive idle time between requests. */
    std::chrono::milliseconds idle_timeout{60000};

    /** @brief listen(2) backlog. */
    int backlog{512};
    /** @brief Set SO_REUSEADDR on listeners. */
    bool reuse_addr{true};

    /** @brief Worker threads; 0 means hardware_concurrency(). */
    unsigned workers{0};

    /** @brief Reject header sections larger than this (431). */
    std::size_t max_header_size{16 * 1024};
    /** @brief Reject bodies larger than this (413). */
    std::size_t max_body_size{16 * 1024 * 1024};

    /** @brief Response compression settings. */
    compression::Options compression{};

    /** @brief Enable HTTPS by providing certificate options. */
    std::optional<TlsOptions> tls{};
};

/**
 * @brief The HTTP/HTTPS server.
 *
 * Thread-safety: configure routes and middleware before Run(); the routing
 * table is read concurrently by workers afterwards.
 */
class Server {
public:
    /** @brief Create a server with @p opts. */
    explicit Server(ServerOptions opts = {});
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // ------------------------------------------------------------------
    // Routing
    // ------------------------------------------------------------------

    /** @brief Register a route for an explicit method. */
    Route& AddRoute(Method m, std::string_view path, Handler h);

    /** @name Express-style shorthands
     *  @{ */
    Route& Get(std::string_view p, Handler h)    { return AddRoute(Method::GET, p, std::move(h)); }
    Route& Post(std::string_view p, Handler h)   { return AddRoute(Method::POST, p, std::move(h)); }
    Route& Put(std::string_view p, Handler h)    { return AddRoute(Method::PUT, p, std::move(h)); }
    Route& Patch(std::string_view p, Handler h)  { return AddRoute(Method::PATCH, p, std::move(h)); }
    Route& Delete(std::string_view p, Handler h) { return AddRoute(Method::DELETE_, p, std::move(h)); }
    Route& Options(std::string_view p, Handler h){ return AddRoute(Method::OPTIONS, p, std::move(h)); }
    Route& Head(std::string_view p, Handler h)   { return AddRoute(Method::HEAD, p, std::move(h)); }
    Route& Any(std::string_view p, Handler h)    { return AddRoute(Method::ANY, p, std::move(h)); }
    /** @} */

    /**
     * @brief Create a route group under @p prefix.
     * @return Stable reference (owned by the server's router).
     */
    Router::RouteGroup& Group(std::string_view prefix) { return router_.Group(prefix); }

    /** @brief Register global middleware (runs for every request). */
    Server& Use(Middleware mw) { router_.Use(std::move(mw)); return *this; }

    // ------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------

    /**
     * @brief Bind and start serving. Returns immediately.
     * @param ip   Listen address ("0.0.0.0", "::", "127.0.0.1", ...).
     * @param port TCP port. Use 0 for an ephemeral port (see port()).
     * @return false when binding or TLS initialization failed.
     */
    bool Run(std::string_view ip, uint16_t port);

    /** @brief Run("0.0.0.0", port). */
    bool Listen(uint16_t port) { return Run("0.0.0.0", port); }

    /** @brief Block until Stop() is called (from a signal handler, etc.). */
    void Wait();

    /** @brief Gracefully stop: close listeners and all connections. */
    void Stop();

    /** @brief True while the server is running. */
    bool running() const noexcept { return running_.load(std::memory_order_acquire); }

    /** @brief Actual bound port (useful when Run() was given port 0). */
    uint16_t port() const noexcept { return port_; }

    /** @brief Diagnostic message from the last failed Run(). */
    const std::string& last_error() const noexcept { return last_error_; }

    /** @brief Access the underlying router. */
    Router& router() noexcept { return router_; }

private:
    friend class detail::Worker;

    ServerOptions opts_;
    Router router_;

    std::atomic<bool> running_{false};
    uint16_t port_{0};
    std::string last_error_;

    std::vector<std::unique_ptr<detail::Worker>> workers_;
    std::vector<std::thread> threads_;
    std::mutex join_mu_;

    tls::TlsContext tls_ctx_;
    bool tls_enabled_{false};
};

} // namespace socketify
