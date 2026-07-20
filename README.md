<p align="center">
  <img src="assets/logo.png" alt="Socketify logo" width="220">
</p>

<h1 align="center">Socketify</h1>

<p align="center">
  <strong>A fast, modern C++20 HTTP/HTTPS server &amp; routing framework</strong><br>
  Express-style ergonomics on an epoll event loop with zero-copy file serving.
</p>

<p align="center">
  <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square&logo=cplusplus&logoColor=white">
  <img alt="CMake" src="https://img.shields.io/badge/CMake-3.16%2B-064F8C?style=flat-square&logo=cmake&logoColor=white">
  <img alt="Linux" src="https://img.shields.io/badge/OS-Linux-FCC624?style=flat-square&logo=linux&logoColor=black">
  <img alt="epoll" src="https://img.shields.io/badge/I%2FO-epoll%20%2B%20SO__REUSEPORT-0D9488?style=flat-square">
  <img alt="TLS" src="https://img.shields.io/badge/TLS-OpenSSL-721412?style=flat-square&logo=openssl&logoColor=white">
  <img alt="Tests" src="https://img.shields.io/badge/tests-152%20passing-22C55E?style=flat-square">
  <img alt="License" src="https://img.shields.io/badge/license-Source--Available-orange?style=flat-square">
  <img alt="Version" src="https://img.shields.io/badge/version-0.2.0-blue?style=flat-square">
</p>

<p align="center">
  <code>http</code> · <code>https</code> · <code>rest-api</code> · <code>middleware</code> · <code>sse</code> ·
  <code>sessions</code> · <code>cors</code> · <code>rate-limit</code> · <code>gzip</code> ·
  <code>static-files</code> · <code>sendfile</code> · <code>json</code> · <code>multipart</code> ·
  <code>cookies</code> · <code>high-performance</code> · <code>zero-copy</code> ·
  <code>web-framework</code> · <code>backend</code> · <code>cpp20</code> · <code>cmake</code>
</p>

---

## Quick taste

```cpp
#include <socketify/socketify.h>
using namespace socketify;

int main() {
    Server server;

    server.Get("/", [](Request&, Response& res) {
        res.send("Hello, world!\n");
    });

    server.Get("/users/:id", [](Request& req, Response& res) {
        res.json({{"id", req.params().at("id")}});
    });

    server.Listen(8080);
    server.Wait();
}
```

## Features

- **HTTP/1.1** — keep-alive, pipelining, chunked transfer decoding,
  `Expect: 100-continue`, configurable header/body limits and timeouts
- **HTTPS** — TLS 1.2+ via OpenSSL, cert/key from files or environment,
  one code path for HTTP and HTTPS
- **Routing** — `:params`, `*wildcards`, route groups, per-route and global
  middleware, automatic `HEAD` fallback and 405 handling
- **Request/response** — lazy query/cookie parsing, JSON body
  (`nlohmann::json`), urlencoded forms, multipart file uploads,
  streaming/chunked responses, redirects
- **Static files** — zero-copy `sendfile(2)`, ETag/Last-Modified,
  Range requests, directory indexes, SPA fallthrough
- **Middleware built-ins** — request logging, request IDs, CORS,
  token-bucket rate limiting (`RateLimit-*` headers), gzip/deflate
  compression, body-size limits
- **Sessions** — HMAC-SHA256 signed cookies, in-memory TTL store,
  pluggable `Store` interface
- **Server-Sent Events** — `sse::upgrade()` with a thread-safe session
  handle for pushing events from any thread
- **Performance architecture** — one epoll loop per worker thread,
  `SO_REUSEPORT` listeners (no accept contention), non-blocking sockets,
  buffered writes, timer-based connection expiry

## Requirements

| Dependency | Notes |
|---|---|
| Linux | epoll / `sendfile` / `SO_REUSEPORT` |
| C++20 compiler | GCC 12+ or Clang 15+ |
| CMake | ≥ 3.16 |
| [nlohmann_json](https://github.com/nlohmann/json) | ≥ 3.11 |
| ZLIB | gzip / deflate |
| OpenSSL 1.1.1+ | optional; only when `SOCKETIFY_WITH_TLS=ON` (default) |

```bash
# Debian / Ubuntu
sudo apt install build-essential cmake ninja-build \
    nlohmann-json3-dev zlib1g-dev libssl-dev
```

## How to build

### 1. Clone

```bash
git clone https://github.com/yourname/socketify.git
cd socketify
```

### 2. Configure & build (Release)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DSOCKETIFY_BUILD_EXAMPLES=ON
cmake --build build -j$(nproc)
```

Or use the helper script:

```bash
./scripts/build_release.sh
```

### 3. Debug build + sanitizers + tests

```bash
./scripts/build_debug.sh
# equivalent to:
#   cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug \
#       -DSOCKETIFY_BUILD_TESTS=ON \
#       -DSOCKETIFY_SANITIZE=address,undefined
#   cmake --build build-debug -j$(nproc)
#   ctest --test-dir build-debug --output-on-failure
```

### 4. Install (optional)

```bash
cmake --install build --prefix /usr/local
# then in your app:
#   find_package(Socketify 0.2 REQUIRED)
#   target_link_libraries(app PRIVATE Socketify::socketify)
```

### CMake options

| Option | Default | Meaning |
|---|---|---|
| `SOCKETIFY_WITH_TLS` | `ON` | HTTPS support (needs OpenSSL) |
| `SOCKETIFY_BUILD_EXAMPLES` | `OFF` | Build `examples/` |
| `SOCKETIFY_BUILD_TESTS` | `OFF` | Build GoogleTest suite |
| `SOCKETIFY_BUILD_DOCS` | `OFF` | Add a `docs` Doxygen target |
| `SOCKETIFY_SANITIZE` | *(empty)* | e.g. `address,undefined` |
| `SOCKETIFY_WERROR` | `OFF` | Treat warnings as errors |

Convenience scripts:

| Script | What it does |
|---|---|
| `scripts/build_release.sh` | Optimized Release build (+ examples) |
| `scripts/build_debug.sh` | Debug + ASan/UBSan + tests |
| `scripts/run_tests.sh` | Build (if needed) and run CTest |
| `scripts/run_examples.sh [01-07]` | Build & run one graded example |
| `scripts/serve_docs.sh [port]` | Generate Doxygen docs, serve on localhost, open browser |

## Using it in your project

After `cmake --install build`:

```cmake
find_package(Socketify 0.2 REQUIRED)
target_link_libraries(app PRIVATE Socketify::socketify)
```

Or add Socketify as a subdirectory / FetchContent and link `Socketify::socketify`.

## Examples — a graded tour

| Example | Shows |
|---|---|
| [`01_hello_world`](examples/01_hello_world) | routes, path params, JSON |
| [`02_rest_api`](examples/02_rest_api) | CRUD API, groups, body parsing, status codes |
| [`03_middleware`](examples/03_middleware) | logging, CORS, rate limit, sessions, custom auth |
| [`04_static_site`](examples/04_static_site) | static files, compression, SPA fallback |
| [`05_sse_chat`](examples/05_sse_chat) | live feed over Server-Sent Events |
| [`06_https`](examples/06_https) | TLS with a self-signed dev cert |
| [`07_fullstack`](examples/07_fullstack) | everything combined: frontend + API + sessions + SSE |

```bash
./scripts/run_examples.sh 07   # build and run the fullstack guestbook
```

## Tests

152 unit and integration tests (GoogleTest), run under
AddressSanitizer/UBSan in the debug build:

```bash
./scripts/run_tests.sh
```

## Documentation

- Hand-written guide: <a href="docs/API.md">docs/API.md</a>
- **API reference (local only)** — generate with the script; HTML is written
  to `docs/generated/` which is **gitignored** (not pushed to GitHub):

```bash
./scripts/serve_docs.sh          # regen + http://127.0.0.1:8765/
./scripts/serve_docs.sh 9000     # custom port
./scripts/serve_docs.sh --regen-only
```

Requires `doxygen` on `PATH` (or under `.deps/sysroot/usr/bin/doxygen`).

## Benchmarks

Same-machine **JSON `/ping`** micro-benchmark (`{"ok":true}`), measured with
[wrk](https://github.com/wg/wrk) (`-t4 -c100 -d8s`), localhost keep-alive,
no database. Regenerated on **2026-07-20** (Intel i7-12700K, 20 threads, Linux).

Charts are **animated SVGs** with a **transparent background** (text/grid adapt to
light & dark themes via `prefers-color-scheme`).

<p align="center">
  <img src="assets/benchmark_rps.svg" alt="Requests per second comparison (animated SVG)" width="920">
</p>

| Framework | req/s | avg latency | p99 latency | vs Socketify |
|---|---:|---:|---:|---:|
| **Socketify** (epoll, Release) | **909,254** | **0.080 ms** | **0.124 ms** | 1.0× |
| Express 4 (Node.js) | 61,208 | 1.98 ms | 2.30 ms | ~15× slower |
| Flask 3 + Waitress | 1,862 | 86.3 ms | 1140 ms | ~488× slower |
| Django 4 + Waitress (no middleware) | 1,202 | 82.5 ms | 123 ms | ~757× slower |

<p align="center">
  <img src="assets/benchmark_latency.svg" alt="P99 latency comparison (animated SVG)" width="920">
</p>

### How this compares to public numbers

Independent suites (different hardware / clients — **not** apples-to-apples
with the table above) still show the same *ordering* for dynamic languages:

| Source | Express | Flask | Django |
|---|---:|---:|---:|
| [Sharkbench](https://sharkbench.dev/web/python) (2025-08, Ryzen 7800X3D) | ~5,766 req/s | ~1,092 (Gunicorn) | ~950 (Gunicorn) |
| Commodity `/ping` write-up ([Medium](https://augustinejoseph.medium.com/fastapi-vs-django-vs-django-ninja-vs-fastify-vs-express-a-real-world-performance-benchmark-on-0b0fd1db9eb0)) | ~6,500 req/s | — | much lower with default middleware |

Socketify’s advantage comes from **native C++20**, an **epoll + `SO_REUSEPORT`**
worker model, and almost no per-request overhead on the hot path — the same
reasons native servers beat interpreted stacks on plaintext/JSON ping tests.

> **Caveats:** micro-benchmarks ≠ your app. Real APIs spend time in DB, auth,
> and business logic. Always measure your workload. Full raw JSON:
> [`benchmarks/results.json`](benchmarks/results.json).

### Reproduce

```bash
./benchmarks/run_all.sh
# optional: DURATION=10 CONCURRENCY=200 ./benchmarks/run_all.sh
```

This builds Release Socketify, spins Flask / Express / Django ping servers,
runs wrk, writes `benchmarks/results.json`, then regenerates the animated
SVGs via `benchmarks/render_charts.py`.

## Roadmap

- WebSockets (frame parser, ping/pong, permessage-deflate)
- HTTP/2 (ALPN, h2c)
- Pluggable auth helpers (JWT, HMAC)
- Redis-backed session/rate-limit stores
- OpenTelemetry exporter

## License & copyright

**Copyright © 2025–2026 M SaLeH NYM. All rights reserved.**

Socketify is released under a **source-available** license (see [LICENSE](LICENSE)):

| You **may** | You **may not** (without written permission) |
|---|---|
| Use Socketify as a library to build & ship **your own apps** | Modify / fork / redistribute **changed** copies of Socketify |
| Link statically or dynamically against an **unmodified** build | Treat Socketify as open-source-to-relicense |

**Want to contribute a patch?** Ask first — email
[saleh.ue4@gmail.com](mailto:saleh.ue4@gmail.com) or Telegram
[t.me/MSaLeHNYM](https://t.me/MSaLeHNYM) for permission. Approved
contributions are assigned to the copyright holder so ownership stays unified.

All copyright and legal ownership of this software belong exclusively to
**M SaLeH NYM**.
