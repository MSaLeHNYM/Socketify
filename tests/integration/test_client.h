#pragma once
// Minimal blocking HTTP test client used by the integration tests.
// Deliberately independent from the framework's own I/O code.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>

namespace testclient {

struct HttpResponse {
    int status{0};
    std::unordered_map<std::string, std::string> headers; // keys lowercased
    std::string body;
    std::string raw;
};

class TcpClient {
public:
    ~TcpClient() { close(); }

    bool connect_to(uint16_t port, const char* host = "127.0.0.1") {
        close();
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return false;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, host, &addr.sin_addr);
        if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            close();
            return false;
        }
        return true;
    }

    bool send_all(std::string_view data) {
        std::size_t off = 0;
        while (off < data.size()) {
            ssize_t n = ::send(fd_, data.data() + off, data.size() - off, MSG_NOSIGNAL);
            if (n <= 0) return false;
            off += static_cast<std::size_t>(n);
        }
        return true;
    }

    // Read until the connection closes or timeout hits.
    std::string read_all(int timeout_ms = 2000) {
        std::string out;
        read_until_(out, timeout_ms, [](const std::string&) { return false; });
        return out;
    }

    // Read until predicate(buffer) is true or timeout.
    template <typename Pred>
    bool read_until(std::string& out, Pred pred, int timeout_ms = 2000) {
        return read_until_(out, timeout_ms, pred);
    }

    // Read exactly one HTTP response (Content-Length framed or till close).
    // Leftover bytes (e.g. a pipelined second response) stay buffered for
    // the next call.
    std::optional<HttpResponse> read_response(int timeout_ms = 2000) {
        // Read headers first (pending_ may already hold them).
        bool got = read_until_(pending_, timeout_ms, [](const std::string& b) {
            return b.find("\r\n\r\n") != std::string::npos;
        });
        if (!got) return std::nullopt;

        HttpResponse r;
        auto hdr_end = pending_.find("\r\n\r\n");
        std::string head = pending_.substr(0, hdr_end);
        std::string buf = pending_;
        r.body = pending_.substr(hdr_end + 4);

        // Status line.
        auto line_end = head.find("\r\n");
        std::string status_line = head.substr(0, line_end);
        if (status_line.size() > 12) r.status = std::atoi(status_line.c_str() + 9);

        // Headers.
        std::size_t pos = line_end + 2;
        while (pos < head.size()) {
            auto eol = head.find("\r\n", pos);
            if (eol == std::string::npos) eol = head.size();
            std::string line = head.substr(pos, eol - pos);
            pos = eol + 2;
            auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string key = line.substr(0, colon);
            for (auto& c : key) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
            std::size_t vs = colon + 1;
            while (vs < line.size() && line[vs] == ' ') ++vs;
            r.headers[key] = line.substr(vs);
        }

        // Body by Content-Length.
        std::size_t consumed_body = r.body.size();
        auto it = r.headers.find("content-length");
        if (it != r.headers.end()) {
            std::size_t want = static_cast<std::size_t>(std::atoll(it->second.c_str()));
            while (r.body.size() < want) {
                std::string more;
                if (!read_until_(more, timeout_ms,
                                 [&](const std::string& b) { return r.body.size() + b.size() >= want; })) {
                    break;
                }
                r.body += more;
                pending_ += more;
            }
            consumed_body = std::min(r.body.size(), want);
            r.body.resize(consumed_body);
        }
        r.raw = buf;
        // Keep any bytes beyond this response for the next read_response().
        pending_.erase(0, hdr_end + 4 + consumed_body);
        return r;
    }

    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    int fd() const { return fd_; }

private:
    template <typename Pred>
    bool read_until_(std::string& out, int timeout_ms, Pred pred) {
        char buf[8192];
        while (!pred(out)) {
            pollfd pfd{fd_, POLLIN, 0};
            int pr = ::poll(&pfd, 1, timeout_ms);
            if (pr <= 0) return pred(out);
            ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
            if (n <= 0) return pred(out);
            out.append(buf, static_cast<std::size_t>(n));
        }
        return true;
    }

    int fd_{-1};
    std::string pending_;
};

// One-shot request helper: connect, send, read one response.
inline std::optional<HttpResponse> request(uint16_t port, std::string_view raw,
                                           int timeout_ms = 2000) {
    TcpClient c;
    if (!c.connect_to(port)) return std::nullopt;
    if (!c.send_all(raw)) return std::nullopt;
    return c.read_response(timeout_ms);
}

inline std::string simple_get(std::string_view path, std::string_view extra_headers = "") {
    std::string r = "GET ";
    r.append(path);
    r += " HTTP/1.1\r\nHost: test\r\n";
    r.append(extra_headers);
    r += "\r\n";
    return r;
}

} // namespace testclient
