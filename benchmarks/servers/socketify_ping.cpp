// Minimal ping server for throughput benchmarks.
#include <socketify/socketify.h>
#include <cstdio>
#include <cstdlib>

using namespace socketify;

int main(int argc, char** argv) {
    uint16_t port = 19080;
    if (argc > 1) port = static_cast<uint16_t>(std::atoi(argv[1]));

    ServerOptions opts;
    opts.workers = 0; // all cores
    opts.compression.min_size = 1u << 30; // effectively off for tiny /ping bodies
    Server server(opts);

    server.Get("/ping", [](Request&, Response& res) {
        res.set_header("Content-Type", "application/json");
        res.send(R"({"ok":true})");
    });

    if (!server.Listen(port)) {
        std::fprintf(stderr, "bind failed: %s\n", server.last_error().c_str());
        return 1;
    }
    std::fprintf(stderr, "socketify ping on %u\n", server.port());
    server.Wait();
    return 0;
}
