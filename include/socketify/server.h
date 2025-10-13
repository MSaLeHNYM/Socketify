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
/**
 * @brief Holds configuration options for the Server.
 */
struct ServerOptions {
    /**
     * @brief Timeout for reading request headers (in milliseconds).
     */
    std::chrono::milliseconds header_timeout{15000};
    /**
     * @brief Timeout for reading the request body (in milliseconds).
     */
    std::chrono::milliseconds body_timeout{30000};
    /**
     * @brief Timeout for idle connections (in milliseconds).
     */
    std::chrono::milliseconds idle_timeout{60000};

    /**
     * @brief The maximum length of the pending connections queue.
     */
    int backlog{128};
    /**
     * @brief Whether to allow multiple sockets to bind to the same port.
     */
    bool reuse_port{false};
    /**
     * @brief Whether to allow the socket to be reused immediately after it is closed.
     */
    bool reuse_addr{true};

    /**
     * @brief The number of worker threads to spawn. If 0, it defaults to the number of hardware threads.
     */
    unsigned workers{0};
    /**
     * @brief The number of acceptor threads to spawn.
     */
    unsigned acceptors{1};

    /**
     * @brief Compression options.
     */
    compression::Options compression{};

    // future TLS options could go here (cert_dir, etc.)
};

// ---------------------------
// Server
// ---------------------------
/**
 * @brief The main HTTP server class.
 *
 * This class is responsible for managing the server lifecycle, including
 * starting and stopping the server, managing routes, and handling incoming
 * connections.
 */
class Server {
public:
    /**
     * @brief Constructs a new Server instance.
     * @param opts The server options.
     */
    explicit Server(ServerOptions opts = {});
    /**
     * @brief Destroys the Server instance and stops the server if it is running.
     */
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    /**
     * @brief Adds a new route to the server.
     * @param m The HTTP method for the route.
     * @param path The URL path for the route.
     * @param h The handler function for the route.
     * @return A reference to the newly created Route.
     */
    Route& AddRoute(Method m, std::string_view path, Handler h);
    /**
     * @brief Creates a new route group.
     * @param prefix The common prefix for all routes in the group.
     * @return A RouteGroup object.
     */
    Router::RouteGroup Group(std::string_view prefix) { return router_.Group(prefix); }
    /**
     * @brief Adds a middleware to the server.
     * @param mw The middleware to add.
     * @return A reference to the Server instance.
     */
    Server& Use(Middleware mw) { router_.Use(std::move(mw)); return *this; }

    /**
     * @brief Starts the server and begins listening for connections.
     * @param ip The IP address to bind to.
     * @param port The port to listen on.
     * @return true if the server started successfully, false otherwise.
     */
    bool Run(std::string_view ip, uint16_t port);
    /**
     * @brief Stops the server.
     */
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