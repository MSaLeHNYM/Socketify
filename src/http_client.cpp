/**
 * @file http_client.cpp
 * @brief Blocking HTTP/1.1 client with optional TLS.
 */

#include "socketify/http_client.h"
#include "socketify/detail/utils.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <memory>
#include <string>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#if SOCKETIFY_HAS_TLS
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

namespace socketify::http_client {

namespace {

struct Url {
    bool tls{false};
    std::string host;
    std::string port;
    std::string target{"/"}; // path + query
};

std::optional<Url> parse_url(const std::string& url) {
    Url u;
    std::string_view sv(url);
    if (detail::istarts_with(sv, "https://")) {
        u.tls = true;
        sv.remove_prefix(8);
    } else if (detail::istarts_with(sv, "http://")) {
        u.tls = false;
        sv.remove_prefix(7);
    } else {
        return std::nullopt;
    }

    auto slash = sv.find('/');
    std::string_view authority = slash == std::string_view::npos ? sv : sv.substr(0, slash);
    if (slash != std::string_view::npos) u.target = std::string(sv.substr(slash));
    if (u.target.empty()) u.target = "/";

    // Strip optional userinfo@ (not forwarded).
    auto at = authority.find('@');
    if (at != std::string_view::npos) authority.remove_prefix(at + 1);

    auto colon = authority.rfind(':');
    if (colon != std::string_view::npos) {
        u.host = std::string(authority.substr(0, colon));
        u.port = std::string(authority.substr(colon + 1));
    } else {
        u.host = std::string(authority);
        u.port = u.tls ? "443" : "80";
    }
    if (u.host.empty()) return std::nullopt;
    return u;
}

void set_timeout(int fd, long timeout_ms) {
    if (timeout_ms <= 0) return;
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

int dial(const Url& u, long timeout_ms, std::string& err) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    int rc = getaddrinfo(u.host.c_str(), u.port.c_str(), &hints, &res);
    if (rc != 0) {
        err = std::string("dns: ") + gai_strerror(rc);
        return -1;
    }
    int fd = -1;
    for (addrinfo* ai = res; ai; ai = ai->ai_next) {
        fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        set_timeout(fd, timeout_ms);
        if (::connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        ::close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) err = "connect failed";
    return fd;
}

// ---- transport abstraction (plain / TLS) ----

struct Transport {
    virtual ~Transport() = default;
    virtual bool write_all(const char* data, std::size_t len) = 0;
    virtual long read_some(char* buf, std::size_t len) = 0; // >0 bytes, 0 eof, <0 error
};

struct PlainTransport : Transport {
    int fd;
    explicit PlainTransport(int f) : fd(f) {}
    ~PlainTransport() override { if (fd >= 0) ::close(fd); }

    bool write_all(const char* data, std::size_t len) override {
        std::size_t sent = 0;
        while (sent < len) {
            ssize_t n = ::send(fd, data + sent, len - sent, MSG_NOSIGNAL);
            if (n <= 0) return false;
            sent += static_cast<std::size_t>(n);
        }
        return true;
    }
    long read_some(char* buf, std::size_t len) override {
        return static_cast<long>(::recv(fd, buf, len, 0));
    }
};

#if SOCKETIFY_HAS_TLS
struct TlsTransport : Transport {
    int fd;
    SSL_CTX* ctx{nullptr};
    SSL* ssl{nullptr};

    explicit TlsTransport(int f) : fd(f) {}
    ~TlsTransport() override {
        if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); }
        if (ctx) SSL_CTX_free(ctx);
        if (fd >= 0) ::close(fd);
    }

    bool handshake(const std::string& host, std::string& err) {
        ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) { err = "SSL_CTX_new failed"; return false; }
        SSL_CTX_set_default_verify_paths(ctx);
        ssl = SSL_new(ctx);
        if (!ssl) { err = "SSL_new failed"; return false; }
        SSL_set_fd(ssl, fd);
        SSL_set_tlsext_host_name(ssl, host.c_str());
        if (SSL_connect(ssl) != 1) { err = "TLS handshake failed"; return false; }
        return true;
    }
    bool write_all(const char* data, std::size_t len) override {
        std::size_t sent = 0;
        while (sent < len) {
            int n = SSL_write(ssl, data + sent, static_cast<int>(len - sent));
            if (n <= 0) return false;
            sent += static_cast<std::size_t>(n);
        }
        return true;
    }
    long read_some(char* buf, std::size_t len) override {
        return SSL_read(ssl, buf, static_cast<int>(len));
    }
};
#endif

std::string method_name(Method m) {
    auto sv = to_string(m);
    return sv.empty() ? std::string("GET") : std::string(sv);
}

// Split "Header: value" lines into the map (case-insensitive keys).
void parse_headers(std::string_view block, HeaderMap& out) {
    for (auto line : detail::split_view(block, '\n')) {
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty()) continue;
        auto colon = line.find(':');
        if (colon == std::string_view::npos) continue;
        auto key = detail::trim_view(line.substr(0, colon));
        auto val = detail::trim_view(line.substr(colon + 1));
        out[std::string(key)] = std::string(val);
    }
}

std::string header_or(const HeaderMap& h, std::string_view key) {
    auto it = h.find(std::string(key));
    return it == h.end() ? std::string{} : it->second;
}

// Decode a chunked transfer-encoded body from raw bytes.
std::string dechunk(std::string_view raw) {
    std::string out;
    std::size_t pos = 0;
    while (pos < raw.size()) {
        auto eol = raw.find("\r\n", pos);
        if (eol == std::string_view::npos) break;
        std::string size_line(raw.substr(pos, eol - pos));
        auto semi = size_line.find(';');
        if (semi != std::string::npos) size_line = size_line.substr(0, semi);
        std::size_t chunk = 0;
        try {
            chunk = static_cast<std::size_t>(std::stoul(size_line, nullptr, 16));
        } catch (...) {
            break;
        }
        pos = eol + 2;
        if (chunk == 0) break;
        if (pos + chunk > raw.size()) break;
        out.append(raw.substr(pos, chunk));
        pos += chunk;
        if (raw.substr(pos, 2) == "\r\n") pos += 2;
    }
    return out;
}

} // namespace

std::optional<nlohmann::json> Response::json() const {
    if (body.empty()) return std::nullopt;
    auto j = nlohmann::json::parse(body, /*cb=*/nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) return std::nullopt;
    return j;
}

Response request(const Request& req) {
    Response resp;

    auto url = parse_url(req.url);
    if (!url) {
        resp.error = "invalid or unsupported URL: " + req.url;
        return resp;
    }
#if !SOCKETIFY_HAS_TLS
    if (url->tls) {
        resp.error = "https requires building Socketify with TLS (SOCKETIFY_WITH_TLS=ON)";
        return resp;
    }
#endif

    int fd = dial(*url, req.timeout_ms, resp.error);
    if (fd < 0) return resp;

    std::unique_ptr<Transport> tr;
#if SOCKETIFY_HAS_TLS
    if (url->tls) {
        auto tls = std::make_unique<TlsTransport>(fd);
        if (!tls->handshake(url->host, resp.error)) return resp;
        tr = std::move(tls);
    } else
#endif
    {
        tr = std::make_unique<PlainTransport>(fd);
    }

    // ---- Build request ----
    std::string wire;
    wire += method_name(req.method);
    wire += ' ';
    wire += url->target;
    wire += " HTTP/1.1\r\n";
    wire += "Host: " + url->host + "\r\n";
    wire += "Connection: close\r\n";
    bool has_ua = false;
    bool has_len = false;
    for (const auto& [k, v] : req.headers) {
        if (detail::iequal_ascii(k, "user-agent")) has_ua = true;
        if (detail::iequal_ascii(k, "content-length")) has_len = true;
        if (detail::iequal_ascii(k, "host") || detail::iequal_ascii(k, "connection")) continue;
        wire += k + ": " + v + "\r\n";
    }
    if (!has_ua) wire += "User-Agent: socketify-http-client\r\n";
    if (!req.body.empty() && !has_len) {
        wire += "Content-Length: " + std::to_string(req.body.size()) + "\r\n";
    }
    wire += "\r\n";
    wire += req.body;

    if (!tr->write_all(wire.data(), wire.size())) {
        resp.error = "write failed";
        return resp;
    }

    // ---- Read full response (Connection: close) ----
    std::string raw;
    char buf[16384];
    while (true) {
        long n = tr->read_some(buf, sizeof(buf));
        if (n > 0) {
            raw.append(buf, static_cast<std::size_t>(n));
        } else if (n == 0) {
            break; // eof
        } else {
            if (raw.empty()) { resp.error = "read failed"; return resp; }
            break;
        }
    }

    auto header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        resp.error = "malformed response (no header terminator)";
        return resp;
    }
    std::string_view head(raw.data(), header_end);
    std::string_view body_raw(raw.data() + header_end + 4, raw.size() - header_end - 4);

    // Status line.
    auto first_eol = head.find("\r\n");
    std::string_view status_line = head.substr(0, first_eol);
    {
        auto sp1 = status_line.find(' ');
        if (sp1 == std::string_view::npos) {
            resp.error = "malformed status line";
            return resp;
        }
        auto rest = status_line.substr(sp1 + 1);
        auto sp2 = rest.find(' ');
        std::string code(rest.substr(0, sp2));
        try {
            resp.status = std::stoi(code);
        } catch (...) {
            resp.error = "malformed status code";
            resp.status = 0;
            return resp;
        }
    }

    parse_headers(head.substr(first_eol + 2), resp.headers);

    if (detail::iequal_ascii(header_or(resp.headers, H_TransferEncoding), "chunked")) {
        resp.body = dechunk(body_raw);
    } else {
        auto cl = header_or(resp.headers, H_ContentLength);
        if (!cl.empty()) {
            try {
                std::size_t len = static_cast<std::size_t>(std::stoul(cl));
                resp.body.assign(body_raw.substr(0, std::min(len, body_raw.size())));
            } catch (...) {
                resp.body.assign(body_raw);
            }
        } else {
            resp.body.assign(body_raw);
        }
    }

    return resp;
}

Response get(const std::string& url, HeaderMap headers) {
    Request req;
    req.method = Method::GET;
    req.url = url;
    req.headers = std::move(headers);
    return request(req);
}

Response post(const std::string& url, std::string body, HeaderMap headers) {
    Request req;
    req.method = Method::POST;
    req.url = url;
    req.body = std::move(body);
    req.headers = std::move(headers);
    if (req.headers.find("Content-Type") == req.headers.end()) {
        req.headers["Content-Type"] = "application/json";
    }
    return request(req);
}

} // namespace socketify::http_client
