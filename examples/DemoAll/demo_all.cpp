#include "socketify/server.h"
#include "socketify/request.h"
#include "socketify/response.h"
#include "socketify/cors.h"
#include "socketify/static_files.h"

#include <nlohmann/json.hpp>
#include <chrono>
#include <thread>
#include <iostream>

using namespace socketify;
using nlohmann::json;

int main() {
    // Configure server with small compression threshold to see it easily
    ServerOptions opts;
    opts.compression.enable = true;
    opts.compression.min_size = 1; // compress basically everything texty
    Server server(opts);

    // --- Global CORS ---
    server.Use(socketify::cors::middleware({
        .allow_origin = "*",          // or set reflect_origin=true for dynamic origin
        .reflect_origin = false,
        .allow_methods = "GET,POST,PUT,PATCH,DELETE,OPTIONS,HEAD",
        .allow_headers = "",          // echo requested headers if empty
        .expose_headers = "X-Server-Info",
        .allow_credentials = false,
        .max_age_seconds = 600,
        .allow_private_network = false,
        .preflight_continue = false
    }));

    // --- Static files ---
    // Serve ./public as the site root (/, /app.js, /style.css, ...)
    server.Use(socketify::static_files::serve({
        .root = "./examples/DemoAll/public",
        .mount = "/",
        .fallthrough = true,
        .auto_index = true,
        .index_names = {"index.html", "index.htm"},
        .directory_listing = false,
        .allow_hidden = false,
        .etag = true,
        .last_modified = true,
        .cache_max_age = 60,   // cache for 60s
        .immutable = false
    }));

    // --- API routes under /api ---
    auto api = server.Group("/api");

    // GET /api/hello  -> { "message": "Hello, World!" }
    api.AddRoute(Method::GET, "/hello", [](Request& req, Response& res) {
        json j = {
            {"message", "Hello, World!"},
            {"path", req.path()},
            {"method", std::string(to_string(req.method()))}
        };
        res.json(j);
    });

    // POST /api/echo  -> echoes JSON body back
    api.AddRoute(Method::POST, "/echo", [](Request& req, Response& res) {
        // If you already parse body JSON elsewhere, here we just demo echoing:
        // For now, treat body as raw string and try to parse if JSON-ish
        try {
            auto j = json::parse(req.body_string().empty() ? "{}" : req.body_string());
            j["echoed"] = true;
            res.json(j);
        } catch (...) {
            // Not JSON → return as raw text payload
            res.set_header(H_ContentType, "text/plain; charset=utf-8");
            res.send(std::string("raw: ") + req.body_string());
        }
    });

    // POST-only endpoint (to showcase 405 on GET)
    api.AddRoute(Method::POST, "/pp", [](Request& req, Response& res) {
        res.set_header("X-Server-Info", "socketify-demo");
        res.send("This is POST /api/pp");
    });

    // Root fallback (dynamic)
    server.AddRoute(Method::GET, "/", [](Request&, Response& res) {
        // Static index.html will normally serve this, but if you turn off static,
        // you'll still see something here.
        res.html("<!doctype html><html><body><h1>Socketify Demo</h1></body></html>");
    });

    // Start server
    const uint16_t PORT = 8080; // use 80 only if you run as root
    if (!server.Run("0.0.0.0", PORT)) {
        std::cerr << "Failed to start server\n";
        return 1;
    }
    std::cout << "Server running at http://127.0.0.1:" << PORT << "\n";

    // Keep process alive
    for (;;) std::this_thread::sleep_for(std::chrono::seconds(1));
}
