# Socketify Guide

A practical, hand-written tour of the framework. For per-symbol reference,
generate the Doxygen docs (`doxygen docs/Doxyfile`).

- [Getting started](#getting-started)
- [Routing](#routing)
- [Request](#request)
- [Response](#response)
- [Body parsing](#body-parsing)
- [Middleware](#middleware)
- [Sessions](#sessions)
- [Static files](#static-files)
- [Server-Sent Events](#server-sent-events)
- [HTTPS / TLS](#https--tls)
- [Server options & tuning](#server-options--tuning)
- [Deployment tips](#deployment-tips)

## Getting started

```cpp
#include <socketify/socketify.h>   // umbrella header — everything you need
using namespace socketify;

int main() {
    Server server;
    server.Get("/", [](Request&, Response& res) {
        res.send("Hello!\n");
    });
    if (!server.Listen(8080)) {
        // last_error() explains bind/TLS failures
        std::fprintf(stderr, "%s\n", server.last_error().c_str());
        return 1;
    }
    server.Wait();   // block until Stop() is called
}
```

`Run(ip, port)` / `Listen(port)` return immediately after spawning the worker
threads; `Wait()` blocks. Call `Stop()` (e.g. from a signal handler) for a
graceful shutdown. Pass port `0` to bind an ephemeral port and read it back
with `server.port()`.

**Threading model:** the server starts N worker threads (default: number of
cores), each with its own epoll loop and `SO_REUSEPORT` listener. Handlers run
synchronously on the loop that owns the connection — keep them fast, and do
blocking work elsewhere.

**Rule of thumb:** register all routes and middleware *before* `Run()`; the
routing table is read concurrently afterwards.

## Routing

```cpp
server.Get   ("/users",      list_users);
server.Post  ("/users",      create_user);
server.Get   ("/users/:id",  get_user);       // req.params().at("id")
server.Delete("/users/:id",  delete_user);
server.Get   ("/files/*path", serve_any);     // wildcard captures the rest
server.Any   ("/ping",       pong);           // any method
```

- `:name` matches one path segment and binds it in `req.params()`.
- `*name` matches the remainder of the path (including slashes).
- `HEAD` requests fall back to the matching `GET` handler (body suppressed).
- A path that matches with the wrong method produces `405 Method Not Allowed`;
  no match at all produces `404`.
- A matched handler that returns without calling `end()`/`send()` gets its
  response auto-finalized (200 with whatever was written).

### Groups

```cpp
auto& api = server.Group("/api/v1");
api.Use(require_auth);                    // group-scoped middleware
api.Get("/todos", list);                  // GET /api/v1/todos
api.Post("/todos", create);
```

### Per-route middleware

`AddRoute`/`Get`/... return a `Route&`, so you can chain:

```cpp
server.Post("/admin/reset", handler).Use(require_admin);
```

## Request

```cpp
req.method();          // Method::GET, POST, ...
req.path();            // decoded, no query string: "/api/users"
req.raw_target();      // as sent: "/api/users?page=2"
req.header("X-Api-Key");   // "" when absent (case-insensitive keys)
req.query();           // ParamMap of decoded query params
req.query_value("page");   // "" when absent
req.params();          // path params bound by the router
req.cookies();         // parsed Cookie header
req.cookie("sid");
req.body_view();       // request body (string_view)
req.remote_ip();       // "203.0.113.7" or "::1"
```

A `Request` is valid only during the handler call — don't keep views to it.
Middleware can attach data for downstream handlers with
`req.set_local(key, shared_ptr)` / `req.local<T>(key)`.

## Response

```cpp
res.status(Status::Created);          // or res.status(201)
res.set_header("X-Frame-Options", "DENY");

res.send("plain text");               // finishes the response
res.html("<h1>hi</h1>");
res.json({{"ok", true}});             // nlohmann::json
res.send_status(Status::NoContent);   // status + reason phrase body
res.redirect("/login");               // 302 (or pass a code)

res.write("chunk 1");                 // incremental body...
res.write("chunk 2");
res.end();                            // ...finalize

res.send_file("assets/logo.png");     // zero-copy sendfile(2)
res.send_file("report.pdf", /*download=*/true, "Q3-report.pdf");
```

Cookies — one `Set-Cookie` header per cookie, built fluently:

```cpp
res.set_cookie(Cookie("theme", "dark")
                   .path("/")
                   .max_age(60 * 60 * 24 * 30)      // seconds
                   .same_site(SameSite::Lax)
                   .http_only(true)
                   .secure(true));
res.clear_cookie("theme");
```

Send helpers return `false` if the response was already ended, so middleware
can safely attempt a response without clobbering one that exists.

## Body parsing

```cpp
#include <socketify/body.h>

// JSON (Content-Type: application/json)
if (auto j = body::json(req)) {
    std::string name = j->value("name", "");
}

// urlencoded form (application/x-www-form-urlencoded)
ParamMap fields = body::form(req);

// multipart/form-data with file uploads
if (auto mp = body::multipart(req)) {
    for (auto& f : mp->files) {
        // f.name, f.filename, f.content_type, f.data
    }
}

body::is_json(req); body::is_form(req); body::is_multipart(req);
```

Bodies are limited by `ServerOptions::max_body_size` (default 16 MiB, 413 on
overflow). Chunked transfer encoding is decoded transparently.

## Middleware

A middleware receives the request, response and a `next()` continuation:

```cpp
server.Use([](Request& req, Response& res, Next next) {
    if (req.header("X-Api-Key") != "secret") {
        res.status(Status::Unauthorized).json({{"error", "unauthorized"}});
        return;              // don't call next() — chain stops here
    }
    next();                  // pass control down the chain
});
```

Built-ins:

```cpp
server.Use(logging::middleware());            // request log (dev/common format)
server.Use(middleware::request_id());         // X-Request-Id
server.Use(middleware::body_limit(1 << 20));  // 413 above 1 MiB
server.Use(cors::middleware());               // permissive CORS by default

ratelimit::Options rl;
rl.capacity = 20;               // burst
rl.refill_per_second = 5.0;     // sustained rate
server.Use(ratelimit::middleware(rl));        // per-IP token bucket,
                                              // RateLimit-* + Retry-After
```

Order matters: middleware runs in registration order, global first, then
group, then per-route.

### Logging

```cpp
logging::set_level(logging::Level::Debug);
logging::info("listening on {}", port);       // {} placeholders
logging::set_sink([](logging::Level, std::string_view line) {
    /* ship to your aggregator */
});
```

## Sessions

Signed session cookies (HMAC-SHA256) with server-side storage:

```cpp
sessions::Options so;
so.secret = "long-random-secret";        // REQUIRED — sign/verify key
so.cookie_name = "sid";                  // default
so.ttl = std::chrono::hours(24);         // default
// so.store = std::make_shared<MyRedisStore>();  // pluggable, default in-memory
server.Use(sessions::middleware(so));

server.Get("/profile", [](Request& req, Response& res) {
    auto sess = sessions::get(req);      // never null below the middleware
    int visits = sess->has("visits") ? sess->get("visits").get<int>() : 0;
    sess->set("visits", visits + 1);     // persisted automatically
    res.json({{"visits", visits + 1}});
});
```

`sess->destroy()` deletes the server-side entry and expires the cookie. A
tampered or expired cookie yields a fresh session (`sess->is_new()`).
Custom stores implement `sessions::Store` (`load` / `save` / `destroy`).

## Static files

```cpp
server.Use(static_files::serve("public"));                 // mount at "/"
server.Use(static_files::serve("dist", {.mount = "/assets",
                                        .cache_max_age = 86400,
                                        .immutable = true}));
```

- Streams with `sendfile(2)` — large files never touch userspace memory.
- `ETag` + `Last-Modified` conditional requests (304), single `Range`
  requests (206) out of the box.
- Path traversal is blocked; dotfiles are hidden unless `allow_hidden`.
- `fallthrough = true` (default) calls `next()` on miss — put an SPA
  fallback route after it; `false` answers 404 directly.
- `directory_listing = true` renders a simple HTML index.

## Server-Sent Events

```cpp
#include <socketify/sse.h>

server.Get("/events", [&](Request& req, Response& res) {
    sse::Session s = sse::upgrade(req, res);   // takes over the connection
    s.comment("connected");                    // ": connected"
    hub.add(std::move(s));                     // keep it anywhere
});

// later, from ANY thread:
session.send("tick");                          // data: tick
session.send_event("update", payload, "42");   // event/data/id
session.close();
```

`sse::Session` is a thread-safe handle; sends return `false` once the client
disconnected, which is the natural point to drop the handle. See
`examples/05_sse_chat` and `examples/07_fullstack` for broadcast hubs.

## HTTPS / TLS

```cpp
ServerOptions opts;
opts.tls = TlsOptions{.cert_file = "server.crt", .key_file = "server.key"};
// or from SOCKETIFY_CERT_FILE / SOCKETIFY_KEY_FILE:
if (auto env = TlsOptions::from_env()) opts.tls = *env;

Server server(opts);
server.Run("0.0.0.0", 8443);   // false + last_error() on bad cert/key
```

TLS 1.2 is the minimum by default (`min_version`), and the cipher list is
configurable. HTTP and HTTPS share one code path — everything above works
identically. Build with `-DSOCKETIFY_WITH_TLS=OFF` to drop the OpenSSL
dependency. Generate a dev cert with `examples/06_https/gen_cert.sh`.

## Server options & tuning

```cpp
ServerOptions opts;
opts.workers         = 4;                 // 0 = hardware cores (default)
opts.max_header_size = 16 * 1024;         // 431 above this
opts.max_body_size   = 16 * 1024 * 1024;  // 413 above this
opts.header_timeout  = std::chrono::seconds(15);
opts.body_timeout    = std::chrono::seconds(30);
opts.idle_timeout    = std::chrono::seconds(60);  // keep-alive idle
opts.compression.min_size = 1024;         // gzip/deflate threshold
Server server(opts);
```

## Deployment tips

- **Reverse proxy or edge?** Socketify is comfortable at the edge (TLS,
  compression, static files are built in), but behind nginx/haproxy remember
  the client address is the proxy's — trust `X-Forwarded-For` only from your
  proxy.
- **Graceful shutdown:** call `Stop()` from a `SIGINT`/`SIGTERM` handler and
  let `Wait()` return; in-flight connections are closed cleanly.
- **Keep handlers non-blocking.** A blocked handler blocks every connection
  on that worker's loop. Offload slow work (DB calls, disk crunching) to a
  thread pool and reply via SSE or a follow-up request.
- **File descriptors:** each connection is one fd; raise `ulimit -n` for
  high-concurrency deployments.
- **Sessions in production:** the default `MemoryStore` is per-process. With
  multiple processes or hosts, implement `sessions::Store` over Redis or a
  database, and keep the same `secret` across instances.
- **Sanitized CI builds:** `./scripts/build_debug.sh` configures
  ASan+UBSan and runs the test suite — wire it into CI as-is.
