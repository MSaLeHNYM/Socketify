Socketify
Socketify is a lightweight, high-performance C++ library for building scalable server applications and APIs using sockets. It simplifies asynchronous TCP/UDP networking, making it ideal for creating RESTful APIs, real-time applications, and custom network protocols. Socketify is cross-platform, efficient, and easy to use, designed for developers who need fast and reliable server solutions.
Features

🚀 High Performance: Leverages asynchronous socket I/O for low-latency, high-throughput servers.
🛠️ Simple API: Intuitive interface for building HTTP/REST servers and custom protocols.
🔄 Asynchronous Support: Scales to handle thousands of connections with minimal resource usage.
🌐 Cross-Platform: Compatible with Linux, macOS, and Windows.
📦 Minimal Dependencies: Lightweight design to keep your project lean.
📜 MIT License: Freely use, modify, and distribute for any purpose.

Getting Started
Prerequisites

C++17 or later
A C++ compiler (e.g., GCC, Clang, MSVC)
CMake 3.10 or later (for building)
Optional: Boost.Asio or libevent for advanced asynchronous features

Installation

Clone the repository:git clone https://github.com/MSaLeHNYM/socketify.git
cd socketify


Build the library using CMake:mkdir build && cd build
cmake ..
make


Install the library (optional):sudo make install



Usage
Below is a simple example of a TCP server using Socketify to handle HTTP-like requests:
#include <socketify/socketify.h>
#include <iostream>

int main() {
    // Initialize Socketify server
    socketify::Server server(8080); // Listen on port 8080

    // Define request handler
    server.on_request([](socketify::Request& req, socketify::Response& res) {
        res.set_status(200);
        res.set_body("Hello, World!");
        res.send();
    });

    // Start the server
    std::cout << "Server running on http://localhost:8080\n";
    server.run();

    return 0;
}

Compile and run:
g++ -o server example.cpp -lsocketify
./server

Visit http://localhost:8080 in a browser or use curl to see the response.
Building Your Own Server
Socketify provides a flexible API for creating custom servers:

TCP/UDP Support: Build servers for reliable or low-latency communication.
Asynchronous I/O: Use non-blocking sockets for scalability.
HTTP Parsing: Easily handle RESTful API requests (planned feature).

See the examples/ directory for more use cases.
Contributing
Contributions are welcome! To contribute:

Fork the repository.
Create a feature branch (git checkout -b feature/your-feature).
Commit your changes (git commit -m "Add your feature").
Push to the branch (git push origin feature/your-feature).
Open a pull request.

Please follow the Code of Conduct and ensure tests pass.
License
Socketify is licensed under the MIT License. Copyright (c) 2025 MSaLeHNYM.
Contact
For questions or suggestions, open an issue or reach out via GitHub: MSaLeHNYM.
🌟 Star this repo if you find Socketify useful!
