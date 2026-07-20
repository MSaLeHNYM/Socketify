# Socketify {#mainpage}

\htmlonly
<div style="text-align:center;margin:1.5rem 0 0.5rem">
  <img src="logo.png" alt="Socketify logo" width="160" style="max-width:40%;height:auto"/>
</div>
<div style="text-align:center;margin-bottom:1.5rem">
  <strong>A fast, modern C++20 HTTP/HTTPS server &amp; routing framework</strong><br/>
  Express-style ergonomics on an epoll event loop with zero-copy file serving.
</div>
\endhtmlonly

## Quick taste

```cpp
#include <socketify/socketify.h>
using namespace socketify;

int main() {
    Server server;
    server.Get("/", [](Request&, Response& res) {
        res.send("Hello, world!\n");
    });
    server.Listen(8080);
    server.Wait();
}
```

## Browse the API

| Topic | Start here |
|---|---|
| Server & options | socketify::Server, socketify::ServerOptions |
| Request / Response | socketify::Request, socketify::Response |
| Routing | socketify::Router |
| Middleware | socketify::Middleware, socketify::cors, socketify::ratelimit |
| Sessions | socketify::sessions |
| TLS | socketify::TlsOptions, socketify::tls::TlsContext |
| SSE | socketify::sse |
| Static files | socketify::static_files |
| Body parsers | socketify::body |

Hand-written guide: see `docs/API.md` in the repository.
Use the class list / file list in the sidebar for the full reference.
