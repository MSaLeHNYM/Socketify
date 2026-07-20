// 01_hello_world — the smallest possible Socketify server.
//
// Build & run:
//   cmake -S . -B build -DSOCKETIFY_BUILD_EXAMPLES=ON && cmake --build build
//   ./build/examples/01_hello_world/example_01_hello_world
//
// Try it:
//   curl http://localhost:8080/
//   curl http://localhost:8080/hello/you

#include <socketify/socketify.h>

#include <cstdio>

using namespace socketify;

int main() {
    Server server;

    server.Get("/", [](Request&, Response& res) {
        res.send("Hello, world!\n");
    });

    // Path parameters bind into req.params().
    server.Get("/hello/:name", [](Request& req, Response& res) {
        res.send("Hello, " + req.params().at("name") + "!\n");
    });

    // JSON in one line.
    server.Get("/json", [](Request&, Response& res) {
        res.json({{"message", "hello"}, {"framework", "socketify"}});
    });

    if (!server.Run("0.0.0.0", 8080)) {
        std::fprintf(stderr, "failed to start: %s\n", server.last_error().c_str());
        return 1;
    }
    std::printf("listening on http://localhost:8080\n");
    server.Wait(); // block until Stop() (e.g. from a signal handler)
    return 0;
}
