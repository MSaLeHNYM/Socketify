# Socketify

Socketify is a modern C++ web server framework inspired by the simplicity and flexibility of projects like Flask and Express, but built for high performance and low-level control.
It provides a clean API to build HTTP services with features like routing, middleware, and extensibility, while giving you the power of native C++.

---

## ✨ Features

- **HTTP/1.1 Server**: A robust and efficient HTTP/1.1 server.
- **Routing**: A flexible routing system with support for path parameters and wildcards.
- **Middleware**: A powerful middleware system for cross-cutting concerns like logging, authentication, and more.
- **Static Files**: A middleware for serving static files with support for caching and range requests.
- **CORS**: A middleware for handling Cross-Origin Resource Sharing (CORS).
- **Compression**: A middleware for compressing responses with Gzip or Deflate.
- **JSON Support**: Built-in support for JSON with the nlohmann/json library.
- **Scalable Architecture**: A modular design that is easy to extend and scale.

---

## 📦 Build

Socketify uses **CMake** as its build system. You will need a C++ compiler that supports C++17.

### Dependencies

- **zlib**: For compression.
- **nlohmann/json**: For JSON support.

On Debian/Ubuntu, you can install the dependencies with:

```bash
sudo apt-get update
sudo apt-get install -y zlib1g-dev nlohmann-json3-dev
```

### Building the Project

```bash
git clone https://github.com/yourname/socketify.git
cd socketify
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Building the Examples

To build the examples, run CMake with the `SOCKETIFY_BUILD_EXAMPLES` option:

```bash
cmake .. -DSOCKETIFY_BUILD_EXAMPLES=ON
make -j$(nproc)
```

The examples will be located in the `build/examples` directory.

---

## 🚀 Usage

Here is a simple example of a Socketify server:

```cpp
#include <socketify/server.h>
#include <iostream>

int main() {
    socketify::Server server;

    server.AddRoute(socketify::Method::GET, "/", [](auto&, auto& res) {
        res.html("<h1>Hello, World!</h1>");
    });

    server.AddRoute(socketify::Method::GET, "/hello/:name", [](auto& req, auto& res) {
        std::string name = req.params().at("name");
        res.send("Hello, " + name + "!");
    });

    if (!server.Run("0.0.0.0", 3000)) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }

    return 0;
}
```

To compile and run this example, save it as `main.cpp` and run:

```bash
g++ -std=c++17 main.cpp -o main -lsocketify -I/path/to/socketify/include -L/path/to/socketify/build
./main
```

Then, you can access the server at `http://localhost:3000`.

---

## 🗺️ Roadmap

- WebSocket support
- HTTP/2 support
- Pluggable authentication (JWT, HMAC)
- Redis integration for rate-limiting and sessions
- OpenTelemetry exporter