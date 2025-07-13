# 🚀 Socketify

**Socketify** is a lightweight, high-performance C++ library for building scalable server applications and APIs using sockets. It simplifies asynchronous TCP/UDP networking, making it ideal for creating RESTful APIs, real-time applications, and custom network protocols.

> **Cross-platform. Asynchronous. Minimal. Blazingly fast.**

---

## ✨ Features

- ⚡ **High Performance**  
  Leverages asynchronous socket I/O for low-latency, high-throughput servers.

- 🔧 **Simple API**  
  Intuitive interface for building HTTP/REST servers and custom protocols.

- 🔁 **Asynchronous Support**  
  Handles thousands of concurrent connections efficiently.

- 🖥 **Cross-Platform**  
  Runs on **Linux**, **macOS**, and **Windows**.

- 📦 **Minimal Dependencies**  
  Lightweight design keeps your project fast and lean.

- 📝 **MIT License**  
  Freely use, modify, and distribute in commercial or open-source projects.

---

## 🛠 Getting Started

### ✅ Prerequisites

- C++17 or later
- C++ compiler (e.g., **GCC**, **Clang**, **MSVC**)
- **CMake 3.10+**
- *(Optional)* `Boost.Asio` or `libevent` for advanced features

---

## 📥 Installation

```bash
# Clone the repo
git clone https://github.com/MSaLeHNYM/socketify.git
cd socketify

# Build the library
mkdir build && cd build
cmake ..
make

# (Optional) Install the library
sudo make install
