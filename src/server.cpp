#include "socketify/server.h"

#include "socketify/http.h"
#include "socketify/request.h"
#include "socketify/response.h"
#include "socketify/detail/http_parser.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <ctime>
#include <string>
#include <thread>
#include <vector>

namespace socketify {

Server::Server(ServerOptions opts)
    : opts_(std::move(opts)) {}

Server::~Server() {
    Stop();
}

bool Server::Run(std::string_view ip, uint16_t port) {
    if (running_) return true;

    std::string err;
    listen_fd_ = create_listen_socket_(ip, port, opts_.backlog, err);
    if (listen_fd_ < 0) {
        if (on_error_) on_error_(err);
        return false;
    }

    running_ = true;
    accept_thread_ = std::thread(&Server::accept_loop_, this);
    return true;
}

void Server::Stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;
    }
    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (accept_thread_.joinable()) accept_thread_.join();
}

void Server::accept_loop_() {
    while (running_) {
        sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        int fd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len);
        if (fd < 0) {
            if (!running_) break;
            if (errno == EINTR) continue;
            if (on_error_) on_error_(std::string("accept failed: ") + std::strerror(errno));
            continue;
        }

        // For now: a detached thread per connection (simple, not optimal).
        std::thread(&Server::handle_connection_, this, fd).detach();
    }
}

void Server::handle_connection_(int client_fd) {
    // Simple keep-alive: process multiple requests until client closes or we say close
    bool keep_alive = true;

    while (keep_alive) {
        detail::HttpParser parser;
        Request req;
        Response res;

        // Blocking read loop for one message (start-line + headers + body)
        char buf[8192];

        // Set socket receive timeout for header phase
        {
            timeval tv{};
            tv.tv_sec  = opts_.read_header_timeout_ms / 1000;
            tv.tv_usec = (opts_.read_header_timeout_ms % 1000) * 1000;
            ::setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        }

        // Read until headers complete (and possibly body if already available)
        while (!parser.complete() && !parser.error()) {
            ssize_t n = ::recv(client_fd, buf, sizeof(buf), 0);
            if (n == 0) { // client closed
                keep_alive = false;
                break;
            }
            if (n < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // header timeout
                    keep_alive = false;
                    break;
                }
                if (on_error_) on_error_(std::string("recv failed: ") + std::strerror(errno));
                keep_alive = false;
                break;
            }
            std::size_t consumed = parser.consume(buf, static_cast<std::size_t>(n));

            // Safety for unexpected parser stall (shouldn't happen)
            if (consumed == 0 && n > 0 && !parser.complete() && !parser.error()) {
                // Bad input; break to avoid infinite loop
                keep_alive = false;
                break;
            }

            // If headers done and body expected, extend timeout
            if (parser.state() == detail::ParseState::Body) {
                timeval tv{};
                tv.tv_sec  = opts_.read_body_timeout_ms / 1000;
                tv.tv_usec = (opts_.read_body_timeout_ms % 1000) * 1000;
                ::setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            }
        }

        if (!parser.complete()) {
            // If client half-closed silently — just exit.
            if (parser.error()) {
                // 400 Bad Request
                res.status(Status::BadRequest).send("Bad Request\n");
                auto out = serialize_response_(res);
                write_all_(client_fd, out.data(), out.size());
            }
            break; // close connection
        }

        // Populate Request from parser
        req.set_method(parser.method());
        req.set_target(std::string(parser.target()));
        req.set_path(std::string(parser.path()));
        req.set_version(std::string(parser.version()));
        // headers
        auto& hdrs = req.mutable_headers();
        for (const auto& kv : parser.headers()) {
            hdrs.insert(kv);
        }
        // body
        if (!parser.body_view().empty()) {
            // We can reference or own; for safety, own it (copy) here
            req.set_body_storage(std::string(parser.body_view()));
        }

        // Dispatch
        bool matched = router_.dispatch(req, res);
        if (!matched && !res.ended()) {
            res.status(Status::NotFound).send("Not Found\n");
        }
        if (!res.ended()) {
            // Ensure content-length is set
            res.end();
        }

        // Serialize and send
        auto out = serialize_response_(res);
        if (!write_all_(client_fd, out.data(), out.size())) {
            keep_alive = false;
        }

        // Decide connection persistence
        keep_alive = !should_close_(req, res);
        if (!keep_alive) break;

        // Reset socket idle timeout between requests
        {
            timeval tv{};
            tv.tv_sec  = opts_.idle_timeout_ms / 1000;
            tv.tv_usec = (opts_.idle_timeout_ms % 1000) * 1000;
            ::setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        }
    }

    ::shutdown(client_fd, SHUT_RDWR);
    ::close(client_fd);
}

int Server::create_listen_socket_(std::string_view ip, uint16_t port, int backlog, std::string& err) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        err = std::string("socket() failed: ") + std::strerror(errno);
        return -1;
    }

    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_REUSEPORT
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (ip == "0.0.0.0" || ip.empty()) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (::inet_pton(AF_INET, std::string(ip).c_str(), &addr.sin_addr) != 1) {
            err = "inet_pton failed for IP: " + std::string(ip);
            ::close(fd);
            return -1;
        }
    }

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        err = std::string("bind() failed: ") + std::strerror(errno);
        ::close(fd);
        return -1;
    }

    if (::listen(fd, backlog) < 0) {
        err = std::string("listen() failed: ") + std::strerror(errno);
        ::close(fd);
        return -1;
    }

    return fd;
}

bool Server::write_all_(int fd, const void* buf, size_t len) {
    const char* p = static_cast<const char*>(buf);
    size_t off = 0;
    while (off < len) {
        ssize_t n = ::send(fd, p + off, len - off, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        off += static_cast<size_t>(n);
    }
    return true;
}

std::string Server::make_date_header_() {
    // IMF-fixdate, e.g. "Tue, 15 Nov 1994 08:12:31 GMT"
    std::time_t t = std::time(nullptr);
    std::tm gmt{};
#if defined(_WIN32)
    gmtime_s(&gmt, &t);
#else
    gmtime_r(&t, &gmt);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &gmt);
    return std::string(buf);
}

static std::string status_line_(uint16_t code) {
    std::string line = "HTTP/1.1 ";
    line += std::to_string(code);
    line.push_back(' ');
    line += std::string(reason(static_cast<Status>(code)));
    line += "\r\n";
    return line;
}

std::string Server::serialize_response_(const Response& res) {
    std::string out;
    out.reserve(256 + res.body_string().size());

    // Status line
    out += status_line_(res.status_code());

    // Date
    out += "Date: ";
    out += make_date_header_();
    out += "\r\n";

    // Server header
    out += "Server: socketify/0.1\r\n";

    // User headers
    for (const auto& kv : res.headers()) {
        out += kv.first;
        out += ": ";
        out += kv.second;
        out += "\r\n";
    }

    // End headers
    out += "\r\n";

    // Body
    if (res.has_body()) {
        out += res.body_string();
    }

    return out;
}

bool Server::should_close_(const Request& req, const Response& res) {
    // HTTP/1.1 default is keep-alive unless "Connection: close"
    auto find_header = [](const HeaderMap& h, std::string_view key) -> std::string_view {
        auto it = h.find(std::string(key));
        if (it == h.end()) return {};
        return it->second;
    };

    // Check request
    auto rconn = find_header(req.headers(), H_Connection);
    if (!rconn.empty()) {
        std::string v(rconn);
        for (auto& c : v) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        if (v.find("close") != std::string::npos) return true;
    }

    // Check response
    auto sconn = find_header(res.headers(), H_Connection);
    if (!sconn.empty()) {
        std::string v(sconn);
        for (auto& c : v) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        if (v.find("close") != std::string::npos) return true;
        if (v.find("keep-alive") != std::string::npos) return false;
    }

    // HTTP/1.1 default keep-alive
    return false;
}

} // namespace socketify
