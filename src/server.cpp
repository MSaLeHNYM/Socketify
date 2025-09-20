#include "socketify/server.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cassert>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <optional>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "socketify/detail/utils.h"
#include "socketify/detail/socket.h"
#include "socketify/detail/buffer.h"

using namespace std::chrono;

namespace socketify {

// ---------------------------
// small local utils
// ---------------------------
static int set_cloexec_(int fd) {
    int flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

static void set_tcp_opts_(int fd, const ServerOptions& opts) {
    int yes = 1;
    if (opts.reuse_addr) {
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    }
#ifdef SO_REUSEPORT
    if (opts.reuse_port) {
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
    }
#endif
}

static inline char tolower_ascii(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc >= 'A' && uc <= 'Z') return static_cast<char>(uc - 'A' + 'a');
    return static_cast<char>(c);
}
static inline bool iequal_ascii_sv(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (tolower_ascii(a[i]) != tolower_ascii(b[i])) return false;
    return true;
}

// RFC 7231 date
std::string Server::make_date_header_() {
    auto now = system_clock::now();
    auto t = system_clock::to_time_t(now);
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

// ---------------------------
// ctor/dtor
// ---------------------------
Server::Server(ServerOptions opts)
    : opts_(std::move(opts)) {}

Server::~Server() {
    Stop();
}

// ---------------------------
// routing glue
// ---------------------------
Route& Server::AddRoute(Method m, std::string_view path, Handler h) {
    return router_.AddRoute(m, path, std::move(h));
}

// ---------------------------
// run/stop
// ---------------------------
bool Server::Run(std::string_view ip, uint16_t port) {
    if (running_.exchange(true)) return false;

    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        running_ = false;
        return false;
    }

    set_cloexec_(listen_fd_);
    set_tcp_opts_(listen_fd_, opts_);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(std::string(ip).c_str());

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        running_ = false;
        return false;
    }

    if (::listen(listen_fd_, opts_.backlog) < 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        running_ = false;
        return false;
    }

    unsigned n = opts_.workers ? opts_.workers : std::max(1u, std::thread::hardware_concurrency());
    workers_.reserve(n);
    for (unsigned i = 0; i < n; ++i) {
        workers_.emplace_back([this] { accept_loop_(); });
    }
    return true;
}

void Server::Stop() {
    if (!running_.exchange(false)) return;

    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();
}

// ---------------------------
// very small HTTP header parser (start-line + headers)
// ---------------------------
static bool parse_request_headers_(std::string_view raw,
                                   Method& out_method,
                                   std::string& out_path,
                                   HeaderMap& out_headers)
{
    // find end of headers
    size_t hdr_end = raw.find("\r\n\r\n");
    if (hdr_end == std::string_view::npos) return false; // need more data

    // first line
    size_t line_end = raw.find("\r\n");
    if (line_end == std::string_view::npos) return false;

    std::string_view start = raw.substr(0, line_end);
    // METHOD SP REQUEST-TARGET SP HTTP/1.1
    size_t sp1 = start.find(' ');
    if (sp1 == std::string_view::npos) return false;
    size_t sp2 = start.find(' ', sp1 + 1);
    if (sp2 == std::string_view::npos) return false;

    std::string_view m = start.substr(0, sp1);
    std::string_view target = start.substr(sp1 + 1, sp2 - (sp1 + 1));
    // std::string_view ver = start.substr(sp2 + 1);

    out_method = method_from_string(m);
    out_path.assign(target);

    // headers lines
    size_t pos = line_end + 2;
    while (pos < hdr_end) {
        size_t eol = raw.find("\r\n", pos);
        if (eol == std::string_view::npos || eol > hdr_end) break;
        std::string_view line = raw.substr(pos, eol - pos);
        pos = eol + 2;

        if (line.empty()) continue;

        size_t colon = line.find(':');
        if (colon == std::string_view::npos) continue;
        std::string key(line.substr(0, colon));
        // trim SP after colon
        size_t vstart = colon + 1;
        while (vstart < line.size() && (line[vstart] == ' ' || line[vstart] == '\t')) ++vstart;
        std::string val(line.substr(vstart));

        out_headers[std::move(key)] = std::move(val);
    }

    return true;
}

// ---------------------------
// accept + per-connection
// ---------------------------
void Server::accept_loop_() {
    while (running_) {
        sockaddr_in caddr{};
        socklen_t clen = sizeof(caddr);
        int cfd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&caddr), &clen);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            // continue trying
            continue;
        }

        set_cloexec_(cfd);
        handle_connection_(cfd);
        ::close(cfd);
    }
}

void Server::handle_connection_(int client_fd) {
    constexpr size_t BUF_SZ = 16 * 1024;
    std::string inbuf;
    inbuf.resize(BUF_SZ);

    bool keep_alive = true;

    while (running_ && keep_alive) {
        Request req;
        Response res;

        // naive one-shot read (headers only). For bodies, extend this.
        ssize_t rd = ::recv(client_fd, &inbuf[0], inbuf.size(), 0);
        if (rd <= 0) return;

        Method mth = Method::UNKNOWN;
        std::string path;
        HeaderMap hdrs;

        if (!parse_request_headers_(std::string_view(inbuf.data(), static_cast<size_t>(rd)),
                                    mth, path, hdrs))
        {
            res.status(Status::BadRequest).set_header("Connection", "close").send("Bad Request\n");
            std::string out = serialize_response_(req, res, opts_);
            ::send(client_fd, out.data(), out.size(), 0);
            return;
        }

        // Populate request
        req.set_method(mth);
        req.set_path(std::move(path));
        req.mutable_headers() = std::move(hdrs);

        // Dispatch
        if (!router_.dispatch(req, res)) {
            res.status(Status::NotFound).send("Not Found\n");
        } else if (!res.ended()) {
            if (res.status_code() == 0) {
                res.status(Status::OK);
            }
        }

        // Write response
        const std::string out = serialize_response_(req, res, opts_);
        ::send(client_fd, out.data(), out.size(), 0);

        keep_alive = !should_close_(req, res);
    }
}

// ---------------------------
// serialization
// ---------------------------
std::string Server::serialize_response_(const Request& req, const Response& res, const ServerOptions& opts) {
    // Working body (may be empty for HEAD)
    std::string body = res.body_string();

    auto find_header = [](const HeaderMap &h, std::string_view key) -> std::string_view {
        auto it = h.find(std::string(key));
        if (it == h.end()) return {};
        return it->second;
    };

    // Compression
    bool can_compress = false;
    compression::Encoding enc = compression::Encoding::None;

    if (opts.compression.enable && res.has_body() &&
        find_header(res.headers(), "Content-Encoding").empty()) {
        const auto ae = find_header(req.headers(), "Accept-Encoding");
        enc = compression::negotiate_accept_encoding(ae, opts.compression);
        if (enc != compression::Encoding::None) {
            const auto ct = find_header(res.headers(), "Content-Type");
            if (compression::is_compressible_type(ct, opts.compression) &&
                body.size() >= opts.compression.min_size) {
                can_compress = true;
            }
        }
    }

    std::string compressed;
    std::string content_encoding_value;
    if (can_compress) {
        bool ok = false;
        if (enc == compression::Encoding::Gzip) {
            ok = compression::gzip_compress(body, compressed);
            content_encoding_value = "gzip";
        } else if (enc == compression::Encoding::Deflate) {
            ok = compression::deflate_compress(body, compressed);
            content_encoding_value = "deflate";
        }
        if (ok) body.swap(compressed);
        else content_encoding_value.clear();
    }

    // Ensure Content-Type if missing
    std::string forced_content_type;
    if (find_header(res.headers(), "Content-Type").empty()) {
        std::string_view guess = content_type_for_path(req.path());
        if (guess.empty() || guess == "application/octet-stream") {
            auto begins_with = [&](std::string_view s, std::string_view pfx) {
                return s.size() >= pfx.size() &&
                       std::equal(pfx.begin(), pfx.end(), s.begin(),
                                  [](char a, char b){ return (a|32) == (b|32); });
            };
            if (begins_with(body, "<!doctype") || begins_with(body, "<html")) {
                guess = "text/html; charset=utf-8";
            } else {
                guess = "text/plain; charset=utf-8";
            }
        }
        forced_content_type.assign(guess);
    }

    // Build
    std::string out;
    out.reserve(256 + body.size());

    unsigned code = res.status_code() ? res.status_code() : static_cast<unsigned>(Status::OK);
    out += "HTTP/1.1 ";
    out += std::to_string(code);
    out.push_back(' ');
    out += std::string(reason(static_cast<Status>(code)));
    out += "\r\n";

    out += "Date: ";
    out += make_date_header_();
    out += "\r\n";
    out += "Server: socketify/0.1\r\n";

    bool have_vary = false;
    bool have_content_length = false;

    for (const auto &kv : res.headers()) {
        std::string key = kv.first;
        std::string low = key;
        for (auto &c : low) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');

        if (low == "content-length") { have_content_length = true; continue; }
        if (!content_encoding_value.empty() && low == "content-encoding") {
            content_encoding_value.clear();
        }
        if (low == "vary") have_vary = true;

        out += key;
        out += ": ";
        out += kv.second;
        out += "\r\n";
    }

    if (!forced_content_type.empty()) {
        out += "Content-Type: ";
        out += forced_content_type;
        out += "\r\n";
    }

    if (!content_encoding_value.empty()) {
        out += "Content-Encoding: ";
        out += content_encoding_value;
        out += "\r\n";
        if (!have_vary) {
            out += "Vary: Accept-Encoding\r\n";
        }
    }

    if (have_content_length) {
        const auto cl = find_header(res.headers(), "Content-Length");
        out += "Content-Length: ";
        out += std::string(cl);
        out += "\r\n";
    } else {
        out += "Content-Length: ";
        out += std::to_string(body.size()); // entity length; body omitted for HEAD below
        out += "\r\n";
    }

    out += "\r\n";

    if (req.method() != Method::HEAD && !body.empty()) {
        out += body;
    }

    return out;
}

// ---------------------------
// keep-alive
// ---------------------------
bool Server::should_close_(const Request& req, const Response& res) {
    auto find_header = [](const HeaderMap &h, std::string_view key) -> std::string_view {
        auto it = h.find(std::string(key));
        if (it == h.end()) return {};
        return it->second;
    };

    auto rconn = find_header(req.headers(), H_Connection);
    if (!rconn.empty()) {
        if (iequal_ascii_sv(rconn, "close")) return true;
        if (iequal_ascii_sv(rconn, "keep-alive")) return false;
    }

    auto sconn = find_header(res.headers(), H_Connection);
    if (!sconn.empty()) {
        if (iequal_ascii_sv(sconn, "close")) return true;
        if (iequal_ascii_sv(sconn, "keep-alive")) return false;
    }

    // HTTP/1.1 default is keep-alive
    return false;
}

} // namespace socketify
