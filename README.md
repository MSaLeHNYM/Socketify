# Socketify

Socketify is a modern C++ web server framework inspired by simplicity and flexibility of projects like Flask and Express, but built for high performance and low-level control.  
It provides a clean API to build HTTP services with features like routing, middleware, and extensibility, while giving you the power of native C++.

---

## ✨ Features

- **HTTP/1.1 server** with easy routing (`GET`, `POST`, etc.)
- **Middleware support** (logging, static files, compression, etc.)
- **TLS/SSL support** with certificate files or environment variables
- **Configurable options**:
  - Body limits (JSON, multipart, text)
  - Compression
  - CORS
  - Rate limiting
  - Session cookies
  - Static file serving
- **Scalable architecture** with modular design
- **Examples** included (`examples/` folder) for quick start
- Future roadmap:
  - WebSocket support (frame parser, ping/pong, permessage-deflate)
  - HTTP/2 (ALPN, h2c)
  - Pluggable authentication (JWT, HMAC)
  - Redis integration for rate-limit/session
  - OpenTelemetry exporter

---

## 📦 Build

Socketify uses **CMake** as its build system.

```bash
git clone https://github.com/yourname/socketify.git
cd socketify
mkdir build && cd build
cmake ..
make -j$(nproc)
    