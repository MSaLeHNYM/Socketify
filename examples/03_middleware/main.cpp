// 03_middleware — the middleware toolbox: logging, CORS, rate limiting,
// sessions, request ids and a hand-written auth middleware.
//
// Try it:
//   curl -v http://localhost:8080/
//   for i in $(seq 1 15); do curl -s -o /dev/null -w "%{http_code}\n" http://localhost:8080/; done
//   curl -c jar -b jar http://localhost:8080/visits
//   curl http://localhost:8080/admin              # 401
//   curl -H "X-Api-Key: secret" http://localhost:8080/admin

#include <socketify/socketify.h>

#include <cstdio>

using namespace socketify;

int main() {
    Server server;

    // 1. Request logging ("GET / 200 0.1ms 13B" on stderr).
    logging::set_level(logging::Level::Debug);
    server.Use(logging::middleware());

    // 2. Unique id per request (echoed as X-Request-Id).
    server.Use(middleware::request_id());

    // 3. CORS for browser clients.
    server.Use(cors::middleware());

    // 4. Rate limiting: burst of 10, then 1 request/second per IP.
    ratelimit::Options rl;
    rl.capacity = 10;
    rl.refill_per_second = 1.0;
    server.Use(ratelimit::middleware(rl));

    // 5. Signed session cookies.
    sessions::Options so;
    so.secret = "demo-secret-change-me-in-production!";
    server.Use(sessions::middleware(so));

    server.Get("/", [](Request&, Response& res) {
        res.send("try /visits, /admin\n");
    });

    // Session usage: count visits per browser.
    server.Get("/visits", [](Request& req, Response& res) {
        auto sess = sessions::get(req);
        int visits = sess->has("visits") ? sess->get("visits").get<int>() : 0;
        sess->set("visits", ++visits);
        res.json({{"visits", visits}, {"session_id", sess->id()}});
    });

    // A custom per-route middleware: simple API-key auth.
    auto require_key = [](Request& req, Response& res, Next next) {
        if (req.header("X-Api-Key") != "secret") {
            res.status(Status::Unauthorized).json({{"error", "missing or bad X-Api-Key"}});
            return; // short-circuit: handler never runs
        }
        next();
    };

    server.Get("/admin", [](Request&, Response& res) {
        res.json({{"admin", true}});
    }).Use(require_key);

    if (!server.Run("0.0.0.0", 8080)) {
        std::fprintf(stderr, "failed to start: %s\n", server.last_error().c_str());
        return 1;
    }
    std::printf("middleware demo on http://localhost:8080\n");
    server.Wait();
    return 0;
}
