/**
 * @file main.cpp
 * @brief Socketify manager CLI (installed as `socketify`).
 */

#include <socketify/socketify.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace socketify;

namespace {

void print_usage(const char* argv0) {
    std::cerr
        << "Usage: " << argv0 << " [options]\n"
        << "\n"
        << "Options:\n"
        << "  -V, --version           Print Socketify version and exit\n"
        << "  -h, --help              Show this help\n"
        << "      --info              Version and build feature flags\n"
        << "      --run-http <dir>    Serve static files from <dir>\n"
        << "      --host <addr>       Bind address (default 0.0.0.0)\n"
        << "      --port <n>          Bind port (default 8080)\n"
        << "      --index <name>      Index filename for directories\n"
        << "\n"
        << "Examples:\n"
        << "  " << argv0 << " --version\n"
        << "  " << argv0 << " --run-http ./public --port 8080\n";
}

void print_info() {
    std::cout << "Socketify " << SOCKETIFY_VERSION_STRING << "\n"
              << "  TLS:      " << (SOCKETIFY_HAS_TLS ? "yes" : "no") << "\n"
              << "  SQLite:   " << (SOCKETIFY_HAS_SQLITE ? "yes" : "no") << "\n"
              << "  Postgres: " << (SOCKETIFY_HAS_POSTGRES ? "yes" : "no") << "\n"
              << "  MySQL:    " << (SOCKETIFY_HAS_MYSQL ? "yes" : "no") << "\n"
              << "  Mongo:    " << (SOCKETIFY_HAS_MONGO ? "yes" : "no") << "\n";
}

int run_http(const std::string& dir,
             const std::string& host,
             std::uint16_t port,
             const std::string& index) {
    std::error_code ec;
    const fs::path root = fs::absolute(dir, ec);
    if (ec || !fs::is_directory(root, ec)) {
        std::cerr << "error: not a directory: " << dir << "\n";
        return 1;
    }

    static_files::Options so;
    so.root = root.string();
    so.mount = "/";
    so.fallthrough = false;
    so.directory_listing = true;
    so.auto_index = true;
    if (!index.empty()) {
        so.index_names = {index};
    }

    Server server;
    server.Use(logging::middleware());
    const std::string root_str = so.root;
    server.Use(static_files::serve(std::move(so)));

    std::cout << "Serving " << root_str << " on http://" << host << ":" << port
              << "\n";
    if (!server.Run(host, port)) {
        std::cerr << "error: failed to bind " << host << ":" << port << "\n";
        return 1;
    }
    server.Wait();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    bool want_version = false;
    bool want_help = false;
    bool want_info = false;
    std::string run_http_dir;
    std::string host = "0.0.0.0";
    std::uint16_t port = 8080;
    std::string index;

    const std::vector<std::string_view> args(argv + 1, argv + argc);
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto a = args[i];
        auto need = [&](std::string_view flag) -> std::string {
            if (i + 1 >= args.size()) {
                std::cerr << "error: " << flag << " requires an argument\n";
                std::exit(2);
            }
            return std::string(args[++i]);
        };

        if (a == "-V" || a == "--version") {
            want_version = true;
        } else if (a == "-h" || a == "--help") {
            want_help = true;
        } else if (a == "--info") {
            want_info = true;
        } else if (a == "--run-http") {
            run_http_dir = need("--run-http");
        } else if (a == "--host") {
            host = need("--host");
        } else if (a == "--port") {
            const auto s = need("--port");
            char* end = nullptr;
            const long v = std::strtol(s.c_str(), &end, 10);
            if (!end || *end || v <= 0 || v > 65535) {
                std::cerr << "error: invalid --port: " << s << "\n";
                return 2;
            }
            port = static_cast<std::uint16_t>(v);
        } else if (a == "--index") {
            index = need("--index");
        } else {
            std::cerr << "error: unknown option: " << a << "\n";
            print_usage(argv[0]);
            return 2;
        }
    }

    if (want_help || args.empty()) {
        print_usage(argv[0]);
        return want_help ? 0 : 2;
    }
    if (want_version) {
        std::cout << "Socketify " << SOCKETIFY_VERSION_STRING << "\n";
        return 0;
    }
    if (want_info) {
        print_info();
        return 0;
    }
    if (!run_http_dir.empty()) {
        return run_http(run_http_dir, host, port, index);
    }

    print_usage(argv[0]);
    return 2;
}
