// Minimal Pulse echo server for WebSocket throughput benchmarks.
// Clients speak standard RFC 6455; server echoes text via pulse::Hub.
#include <socketify/socketify.h>
#include <cstdio>
#include <cstdlib>

using namespace socketify;

int main(int argc, char** argv) {
    uint16_t port = 19180;
    if (argc > 1) port = static_cast<uint16_t>(std::atoi(argv[1]));

    ServerOptions opts;
    opts.workers = 0; // all cores
    opts.compression.min_size = 1u << 30;
    Server server(opts);

    pulse::Hub hub;

    server.Get("/echo", [&](Request& req, Response& res) {
        auto ch = pulse::upgrade(req, res);
        if (!ch.valid()) return;
        hub.join("echo", ch);
        ch.on_text([&hub](pulse::Channel&, std::string_view msg) {
            hub.to("echo").broadcast_text(msg);
        });
        ch.on_close([&hub](pulse::Channel& c, pulse::CloseCode, std::string_view) {
            hub.leave_all(c);
        });
    });

    // Direct echo (no Hub fan-out) — fair 1:1 ping-pong path
    server.Get("/pong", [](Request& req, Response& res) {
        auto ch = pulse::upgrade(req, res);
        if (!ch.valid()) return;
        ch.on_text([](pulse::Channel& c, std::string_view msg) { c.send_text(msg); });
    });

    if (!server.Listen(port)) {
        std::fprintf(stderr, "bind failed: %s\n", server.last_error().c_str());
        return 1;
    }
    std::fprintf(stderr, "socketify pulse on %u (/pong echo, /echo hub)\n", server.port());
    server.Wait();
    return 0;
}
