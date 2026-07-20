/**
 * @file server.cpp
 * @brief Event-driven server core: SO_REUSEPORT listeners, one epoll loop
 *        per worker, incremental parsing, keep-alive/pipelining, TLS,
 *        sendfile streaming and SSE connection adoption.
 */

#include "socketify/server.h"

#include "socketify/detail/buffer.h"
#include "socketify/detail/file_io.h"
#include "socketify/detail/http_parser.h"
#include "socketify/detail/loop.h"
#include "socketify/detail/socket.h"
#include "socketify/detail/sse_impl.h"
#include "socketify/detail/utils.h"

#include <arpa/inet.h>
#include <csignal>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <unordered_map>

using namespace std::chrono;

namespace socketify {

namespace {

// ---------------------------------------------------------------------------
// Response serialization
// ---------------------------------------------------------------------------

std::string_view find_header_(const HeaderMap& h, std::string_view key) {
    auto it = h.find(std::string(key));
    if (it == h.end()) return {};
    return it->second;
}

/// Serialize status line + headers (+ buffered body) into `out`.
/// For File/Stream responses only the head is emitted; the caller streams
/// the rest.
void serialize_response_(std::string& out,
                         const Request& req,
                         Response& res,
                         const ServerOptions& opts,
                         bool head_request,
                         bool close_connection) {
    std::string body;
    if (res.kind() == Response::Kind::Buffered) {
        body = res.take_body();
    }

    // ---- Compression (buffered responses only) ----
    std::string content_encoding_value;
    if (res.kind() == Response::Kind::Buffered && opts.compression.enable &&
        !body.empty() &&
        find_header_(res.headers(), H_ContentEncoding).empty()) {
        const auto ae = find_header_(req.headers(), H_AcceptEncoding);
        auto enc = compression::negotiate_accept_encoding(ae, opts.compression);
        if (enc != compression::Encoding::None) {
            const auto ct = find_header_(res.headers(), H_ContentType);
            if (compression::is_compressible_type(ct, opts.compression) &&
                body.size() >= opts.compression.min_size) {
                std::string compressed;
                bool ok = false;
                if (enc == compression::Encoding::Gzip) {
                    ok = compression::gzip_compress(body, compressed);
                    content_encoding_value = "gzip";
                } else if (enc == compression::Encoding::Deflate) {
                    ok = compression::deflate_compress(body, compressed);
                    content_encoding_value = "deflate";
                }
                if (ok && compressed.size() < body.size()) {
                    body.swap(compressed);
                } else {
                    content_encoding_value.clear();
                }
            }
        }
    }

    // ---- Default Content-Type for non-empty buffered bodies ----
    std::string forced_content_type;
    if (!body.empty() && find_header_(res.headers(), H_ContentType).empty()) {
        auto begins_with = [&](std::string_view s, std::string_view pfx) {
            return s.size() >= pfx.size() &&
                   std::equal(pfx.begin(), pfx.end(), s.begin(),
                              [](char a, char b) { return (a | 32) == (b | 32); });
        };
        forced_content_type = (begins_with(body, "<!doctype") || begins_with(body, "<html"))
                                  ? "text/html; charset=utf-8"
                                  : "text/plain; charset=utf-8";
    }

    // ---- Head ----
    unsigned code = res.status_code() ? res.status_code() : 200u;
    out.reserve(out.size() + 256 + body.size());
    out += "HTTP/1.1 ";
    out += std::to_string(code);
    out.push_back(' ');
    out += std::string(reason(static_cast<Status>(code)));
    out += "\r\n";

    out += "Date: ";
    out += detail::http_date_now();
    out += "\r\nServer: socketify\r\n";

    bool have_vary = false;
    for (const auto& kv : res.headers()) {
        std::string low = detail::to_lower_copy(kv.first);
        if (low == "content-length" || low == "connection") continue;
        if (low == "vary") have_vary = true;
        out += kv.first;
        out += ": ";
        out += kv.second;
        out += "\r\n";
    }
    for (const auto& sc : res.set_cookies()) {
        out += "Set-Cookie: ";
        out += sc;
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
        if (!have_vary) out += "Vary: Accept-Encoding\r\n";
    }

    switch (res.kind()) {
        case Response::Kind::Buffered:
            out += "Content-Length: ";
            out += std::to_string(body.size());
            out += "\r\n";
            break;
        case Response::Kind::File:
            out += "Content-Length: ";
            out += std::to_string(res.file_length());
            out += "\r\n";
            break;
        case Response::Kind::Stream:
            // Stream length is unknown; the connection closes at the end.
            break;
    }

    out += close_connection ? "Connection: close\r\n" : "Connection: keep-alive\r\n";
    out += "\r\n";

    if (!head_request && !body.empty()) {
        out += body;
    }
}

bool wants_close_(const Request& req, const Response& res) {
    auto rconn = find_header_(req.headers(), H_Connection);
    if (!rconn.empty()) {
        if (detail::iequal_ascii(rconn, "close")) return true;
        if (detail::iequal_ascii(rconn, "keep-alive")) return false;
    }
    auto sconn = find_header_(res.headers(), H_Connection);
    if (!sconn.empty() && detail::iequal_ascii(sconn, "close")) return true;
    // HTTP/1.0 defaults to close; HTTP/1.1 defaults to keep-alive.
    return req.http_version() == "HTTP/1.0";
}

std::string peer_ip_(const sockaddr_storage& ss) {
    char buf[INET6_ADDRSTRLEN] = {0};
    if (ss.ss_family == AF_INET) {
        const auto* a = reinterpret_cast<const sockaddr_in*>(&ss);
        inet_ntop(AF_INET, &a->sin_addr, buf, sizeof(buf));
    } else if (ss.ss_family == AF_INET6) {
        const auto* a = reinterpret_cast<const sockaddr_in6*>(&ss);
        inet_ntop(AF_INET6, &a->sin6_addr, buf, sizeof(buf));
    }
    return buf;
}

} // namespace

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

namespace detail {

class Worker;

/// Token shared with SSE handles; expires with the connection.
struct ConnToken {
    Worker* worker;
    struct Connection* conn;
};

struct Connection {
    Socket sock;
    Buffer in;
    std::string out;
    std::size_t out_off{0};
    HttpParser parser;

    bool close_after{false};
    bool sent_100{false};
    bool head_request{false};
    bool in_request{false}; ///< bytes of the current request already arrived

    // File streaming (after `out` drains).
    FileHandle file;
    std::uint64_t file_off{0};
    std::uint64_t file_end{0};

    // SSE adoption.
    std::shared_ptr<sse::Session::Impl> sse;
    std::shared_ptr<ConnToken> token;

    bool registered_write{false};
    steady_clock::time_point deadline{};

    enum class Phase : std::uint8_t { Handshake, Http, Sse } phase{Phase::Http};

    bool has_pending_output() const {
        return out_off < out.size() || (file.valid() && file_off < file_end);
    }
};

// ---------------------------------------------------------------------------
// Worker: one epoll loop + one SO_REUSEPORT listener
// ---------------------------------------------------------------------------

class Worker {
public:
    Worker(Server& srv) : srv_(srv) {}
    ~Worker() { close_listener_(); }

    bool setup_listener(const std::string& ip, uint16_t port, uint16_t& bound_port,
                        std::string& err);
    void run();
    void request_stop() {
        stop_.store(true, std::memory_order_release);
        loop_.wakeup();
    }

    EventLoop& loop() { return loop_; }

private:
    void accept_new_();
    void on_readable_(Connection* c);
    void process_input_(Connection* c);
    void handle_request_(Connection* c);
    void queue_error_response_(Connection* c, Status st, std::string_view msg);
    void flush_output_(Connection* c);
    void flush_sse_(Connection* c);
    void adopt_sse_(Connection* c, std::shared_ptr<sse::Session::Impl> impl);
    void release_sse_(Connection* c);
    void update_interest_(Connection* c);
    void set_deadline_(Connection* c);
    void sweep_deadlines_();
    void close_conn_(Connection* c);
    void close_listener_() {
        if (listen_fd_ >= 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
    }

    Server& srv_;
    EventLoop loop_;
    int listen_fd_{-1};
    std::atomic<bool> stop_{false};
    std::unordered_map<Connection*, std::unique_ptr<Connection>> conns_;
    char listener_tag_{0};
};

bool Worker::setup_listener(const std::string& ip, uint16_t port, uint16_t& bound_port,
                            std::string& err) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* result = nullptr;
    std::string port_str = std::to_string(port);
    int rc = ::getaddrinfo(ip.empty() ? nullptr : ip.c_str(), port_str.c_str(), &hints, &result);
    if (rc != 0 || !result) {
        err = std::string("getaddrinfo: ") + gai_strerror(rc);
        return false;
    }

    int fd = -1;
    for (addrinfo* ai = result; ai; ai = ai->ai_next) {
        fd = ::socket(ai->ai_family, ai->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
                      ai->ai_protocol);
        if (fd < 0) continue;

        int yes = 1;
        if (srv_.opts_.reuse_addr) {
            ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        }
#ifdef SO_REUSEPORT
        // Per-worker listeners require SO_REUSEPORT (kernel load balancing).
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif
        if (ai->ai_family == AF_INET6) {
            int off = 0;
            ::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
        }

        if (::bind(fd, ai->ai_addr, ai->ai_addrlen) == 0 &&
            ::listen(fd, srv_.opts_.backlog) == 0) {
            break;
        }
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(result);

    if (fd < 0) {
        err = std::string("bind/listen failed: ") + std::strerror(errno);
        return false;
    }

    // Discover the actual port (relevant when port == 0).
    sockaddr_storage ss{};
    socklen_t slen = sizeof(ss);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&ss), &slen) == 0) {
        if (ss.ss_family == AF_INET)
            bound_port = ntohs(reinterpret_cast<sockaddr_in*>(&ss)->sin_port);
        else if (ss.ss_family == AF_INET6)
            bound_port = ntohs(reinterpret_cast<sockaddr_in6*>(&ss)->sin6_port);
    }

    listen_fd_ = fd;
    return true;
}

void Worker::run() {
    loop_.add(listen_fd_, /*read=*/true, /*write=*/false, &listener_tag_);

    std::vector<LoopEvent> events;
    auto last_sweep = steady_clock::now();

    while (!stop_.load(std::memory_order_acquire)) {
        int n = loop_.wait(events, 500);
        if (n < 0) break;

        loop_.run_posted();

        for (const auto& ev : events) {
            if (ev.data == &listener_tag_) {
                accept_new_();
                continue;
            }
            auto* c = static_cast<Connection*>(ev.data);
            if (conns_.find(c) == conns_.end()) continue; // closed earlier this batch

            if (ev.error) {
                close_conn_(c);
                continue;
            }

            if (c->phase == Connection::Phase::Handshake) {
                auto hr = c->sock.handshake();
                if (hr == IoResult::Ok) {
                    c->phase = Connection::Phase::Http;
                    set_deadline_(c);
                    update_interest_(c);
                } else if (hr == IoResult::WantRead) {
                    loop_.mod(c->sock.fd(), true, false, c);
                } else if (hr == IoResult::WantWrite) {
                    loop_.mod(c->sock.fd(), false, true, c);
                } else {
                    close_conn_(c);
                }
                continue;
            }

            if (ev.writable && c->has_pending_output()) {
                flush_output_(c);
                if (conns_.find(c) == conns_.end()) continue;
            }
            if (ev.readable) {
                on_readable_(c);
            }
        }

        auto now = steady_clock::now();
        if (now - last_sweep >= milliseconds(250)) {
            sweep_deadlines_();
            last_sweep = now;
        }
    }

    // Shutdown: close everything owned by this worker.
    close_listener_();
    std::vector<Connection*> all;
    all.reserve(conns_.size());
    for (auto& kv : conns_) all.push_back(kv.first);
    for (auto* c : all) close_conn_(c);
}

void Worker::accept_new_() {
    while (true) {
        sockaddr_storage ss{};
        socklen_t slen = sizeof(ss);
        int cfd = ::accept4(listen_fd_, reinterpret_cast<sockaddr*>(&ss), &slen,
                            SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (cfd < 0) {
            return; // EAGAIN/EINTR or transient error; retry on next readiness
        }

        auto conn = std::make_unique<Connection>();
        conn->sock = Socket(cfd);
        conn->sock.set_remote_ip(peer_ip_(ss));
        conn->parser.set_limits(srv_.opts_.max_header_size, srv_.opts_.max_body_size);

        if (srv_.tls_enabled_) {
            SSL* ssl = srv_.tls_ctx_.new_session(cfd);
            if (!ssl) continue; // drop the connection
            conn->sock.adopt_tls(ssl);
            conn->phase = Connection::Phase::Handshake;
        }

        Connection* raw = conn.get();
        raw->deadline = steady_clock::now() + srv_.opts_.idle_timeout;
        conns_[raw] = std::move(conn);
        loop_.add(raw->sock.fd(), true, false, raw);
    }
}

void Worker::on_readable_(Connection* c) {
    char buf[16 * 1024];
    bool got_data = false;
    while (true) {
        std::size_t got = 0;
        auto r = c->sock.read(buf, sizeof(buf), got);
        if (r == IoResult::Ok) {
            c->in.append(buf, got);
            got_data = true;
            continue;
        }
        if (r == IoResult::WantRead || r == IoResult::WantWrite) break;
        // Closed or Error: if the peer half-closed while we still have
        // output pending, keep flushing; otherwise drop.
        if (c->phase == Connection::Phase::Sse || !c->has_pending_output()) {
            close_conn_(c);
            return;
        }
        c->close_after = true;
        break;
    }
    if (got_data) process_input_(c);
}

void Worker::process_input_(Connection* c) {
    if (c->phase == Connection::Phase::Sse) {
        // Clients may send data on an SSE socket; we discard it.
        c->in.clear();
        return;
    }

    while (true) {
        if (!c->in.empty()) {
            std::size_t used = c->parser.consume(c->in.data(), c->in.size());
            c->in.consume(used);
            if (used > 0) c->in_request = true;
        }

        if (c->parser.error()) {
            queue_error_response_(c, c->parser.error_status(), c->parser.error_message());
            break;
        }

        // Expect: 100-continue — tell the client to send the body.
        if (!c->sent_100 && !c->parser.complete() &&
            (c->parser.state() == ParseState::Body ||
             c->parser.state() == ParseState::ChunkSize)) {
            auto expect = find_header_(c->parser.headers(), "Expect");
            if (detail::iequal_ascii(expect, "100-continue")) {
                c->out += "HTTP/1.1 100 Continue\r\n\r\n";
            }
            c->sent_100 = true;
        }

        if (!c->parser.complete()) break; // need more bytes

        handle_request_(c);
        if (conns_.find(c) == conns_.end()) return; // closed during handling

        c->parser.reset();
        c->sent_100 = false;
        c->in_request = false;

        if (c->close_after || c->phase == Connection::Phase::Sse) break;
        // Wait for the current response (esp. file streaming) to finish
        // before parsing the next pipelined request.
        if (c->file.valid() && c->file_off < c->file_end) break;
        if (c->in.empty()) break;
    }

    set_deadline_(c);
    flush_output_(c);
}

void Worker::handle_request_(Connection* c) {
    Request req;
    Response res;

    req.set_method(c->parser.method());
    req.set_target(std::string(c->parser.target()));
    req.set_version(std::string(c->parser.version()));
    req.set_remote_ip(c->sock.remote_ip());

    // Percent-decode the path.
    std::string decoded_path;
    if (!detail::url_decode(c->parser.path(), decoded_path) ||
        decoded_path.find('\0') != std::string::npos) {
        queue_error_response_(c, Status::BadRequest, "Malformed percent-encoding in path");
        return;
    }
    req.set_path(std::move(decoded_path));

    detail::parse_query_string(c->parser.query_string(), req.mutable_query());

    req.mutable_headers() = c->parser.headers();
    auto cookie_header = find_header_(req.headers(), H_Cookie);
    if (!cookie_header.empty()) {
        cookies::parse_cookie_header(cookie_header, req.mutable_cookies());
    }
    req.set_body_storage(c->parser.take_body());

    c->head_request = (req.method() == Method::HEAD);

    // ---- Route it ----
    bool handled = false;
    try {
        handled = srv_.router_.dispatch(req, res);
    } catch (const std::exception& e) {
        res = Response{};
        res.status(Status::InternalServerError).send(std::string("Internal Server Error: ") + e.what() + "\n");
        handled = true;
    } catch (...) {
        res = Response{};
        res.status(Status::InternalServerError).send("Internal Server Error\n");
        handled = true;
    }

    if (!handled) {
        res.status(Status::NotFound).send("Not Found\n");
    } else if (!res.ended()) {
        // A handler ran but never finalized: auto-finalize with what we have.
        if (res.status_code() == 0) res.status(Status::OK);
        res.end();
    }

    // ---- SSE adoption ----
    if (res.kind() == Response::Kind::Stream) {
        serialize_response_(c->out, req, res, srv_.opts_, c->head_request,
                            /*close_connection=*/true);
        adopt_sse_(c, std::static_pointer_cast<sse::Session::Impl>(res.stream_state()));
        return;
    }

    const bool close_it = wants_close_(req, res);
    if (close_it) c->close_after = true;

    serialize_response_(c->out, req, res, srv_.opts_, c->head_request, close_it);

    // ---- File streaming setup ----
    if (res.kind() == Response::Kind::File && !c->head_request && res.file_length() > 0) {
        if (c->file.open(res.file_path())) {
            c->file_off = res.file_offset();
            c->file_end = res.file_offset() + res.file_length();
        } else {
            // File vanished between send_file() and now; abort cleanly.
            c->close_after = true;
        }
    }
}

void Worker::queue_error_response_(Connection* c, Status st, std::string_view msg) {
    Request dummy;
    Response res;
    std::string body{reason(st)};
    if (!msg.empty()) {
        body.append(": ").append(msg);
    }
    body.push_back('\n');
    res.status(st).send(body);
    c->close_after = true;
    serialize_response_(c->out, dummy, res, srv_.opts_, false, true);
}

void Worker::flush_output_(Connection* c) {
    // 1) Drain the head/body byte buffer.
    while (c->out_off < c->out.size()) {
        std::size_t n = 0;
        auto r = c->sock.write(c->out.data() + c->out_off, c->out.size() - c->out_off, n);
        if (r == IoResult::Ok) {
            c->out_off += n;
            continue;
        }
        if (r == IoResult::WantWrite || r == IoResult::WantRead) {
            update_interest_(c);
            return;
        }
        close_conn_(c);
        return;
    }
    if (c->out_off > 0) {
        c->out.clear();
        c->out_off = 0;
    }

    // 2) Stream the file, if any.
    while (c->file.valid() && c->file_off < c->file_end) {
        std::size_t sent = 0;
        auto r = c->sock.send_file(c->file.fd(), c->file_off,
                                   static_cast<std::size_t>(c->file_end - c->file_off), sent);
        if (r == IoResult::Ok) {
            if (sent == 0) break; // EOF before expected end: give up sending more
            continue;
        }
        if (r == IoResult::WantWrite || r == IoResult::WantRead) {
            update_interest_(c);
            return;
        }
        close_conn_(c);
        return;
    }
    if (c->file.valid() && c->file_off >= c->file_end) {
        c->file.close();
        c->file_off = c->file_end = 0;
    }

    // 3) Response fully sent.
    if (c->phase == Connection::Phase::Sse) {
        flush_sse_(c);
        return;
    }
    if (c->close_after) {
        close_conn_(c);
        return;
    }

    set_deadline_(c);
    update_interest_(c);

    // Pipelined request bytes may already be buffered.
    if (!c->in.empty()) process_input_(c);
}

void Worker::adopt_sse_(Connection* c, std::shared_ptr<sse::Session::Impl> impl) {
    c->phase = Connection::Phase::Sse;
    c->sse = std::move(impl);
    c->close_after = true; // stream ends -> connection closes
    c->token = std::make_shared<ConnToken>(ConnToken{this, c});
    c->deadline = steady_clock::time_point::max();

    std::weak_ptr<ConnToken> wt = c->token;
    EventLoop* loop = &loop_;
    {
        std::lock_guard<std::mutex> lk(c->sse->mu);
        c->sse->notify = [loop, wt]() {
            loop->post([wt]() {
                if (auto t = wt.lock()) {
                    t->worker->flush_sse_(t->conn);
                }
            });
        };
    }
    flush_sse_(c);
}

void Worker::flush_sse_(Connection* c) {
    if (!c->sse) return;

    bool close_requested = false;
    {
        std::lock_guard<std::mutex> lk(c->sse->mu);
        if (!c->sse->pending.empty()) {
            if (c->out_off == c->out.size()) {
                c->out.swap(c->sse->pending);
                c->out_off = 0;
                c->sse->pending.clear();
            } else {
                c->out.append(c->sse->pending);
                c->sse->pending.clear();
            }
        }
        close_requested = c->sse->close_requested;
    }

    // Drain what we can right now.
    while (c->out_off < c->out.size()) {
        std::size_t n = 0;
        auto r = c->sock.write(c->out.data() + c->out_off, c->out.size() - c->out_off, n);
        if (r == IoResult::Ok) {
            c->out_off += n;
            continue;
        }
        if (r == IoResult::WantWrite || r == IoResult::WantRead) {
            update_interest_(c);
            return;
        }
        close_conn_(c);
        return;
    }
    c->out.clear();
    c->out_off = 0;

    if (close_requested) {
        close_conn_(c);
        return;
    }
    update_interest_(c);
}

void Worker::release_sse_(Connection* c) {
    if (!c->sse) return;
    std::lock_guard<std::mutex> lk(c->sse->mu);
    c->sse->closed = true;
    c->sse->notify = nullptr;
    c->sse->pending.clear();
}

void Worker::update_interest_(Connection* c) {
    bool want_write = c->has_pending_output();
    if (want_write != c->registered_write) {
        loop_.mod(c->sock.fd(), true, want_write, c);
        c->registered_write = want_write;
    }
}

void Worker::set_deadline_(Connection* c) {
    if (c->phase == Connection::Phase::Sse) {
        c->deadline = steady_clock::time_point::max();
        return;
    }
    auto now = steady_clock::now();
    if (!c->in_request) {
        c->deadline = now + srv_.opts_.idle_timeout;
    } else {
        switch (c->parser.state()) {
            case ParseState::Body:
            case ParseState::ChunkSize:
            case ParseState::ChunkData:
            case ParseState::ChunkDataEnd:
            case ParseState::ChunkTrailer:
                c->deadline = now + srv_.opts_.body_timeout;
                break;
            default:
                c->deadline = now + srv_.opts_.header_timeout;
                break;
        }
    }
}

void Worker::sweep_deadlines_() {
    auto now = steady_clock::now();
    std::vector<Connection*> expired;
    for (auto& kv : conns_) {
        if (kv.first->deadline <= now) expired.push_back(kv.first);
    }
    for (auto* c : expired) {
        if (c->in_request && c->phase == Connection::Phase::Http && !c->has_pending_output()) {
            queue_error_response_(c, Status::RequestTimeout, "");
            flush_output_(c); // may close; otherwise close on drain
            // Force close even if flushing stalls: leave close_after set and
            // give the flush one epoll round; the deadline stays in the past
            // so the next sweep closes it for good.
            if (conns_.find(c) != conns_.end()) {
                c->deadline = now + srv_.opts_.header_timeout;
            }
        } else {
            close_conn_(c);
        }
    }
}

void Worker::close_conn_(Connection* c) {
    release_sse_(c);
    c->token.reset();
    if (c->sock.valid()) {
        loop_.del(c->sock.fd());
    }
    conns_.erase(c);
}

} // namespace detail

// ---------------------------------------------------------------------------
// Server
// ---------------------------------------------------------------------------

Server::Server(ServerOptions opts) : opts_(std::move(opts)) {}

Server::~Server() {
    Stop();
    Wait();
}

Route& Server::AddRoute(Method m, std::string_view path, Handler h) {
    return router_.AddRoute(m, path, std::move(h));
}

bool Server::Run(std::string_view ip, uint16_t port) {
    if (running_.exchange(true)) return false;

    // TLS writes go through write(2) and cannot use MSG_NOSIGNAL; a peer
    // reset would otherwise kill the process with SIGPIPE.
    ::signal(SIGPIPE, SIG_IGN);

    // TLS setup (before any listener is up).
    tls_enabled_ = false;
    if (opts_.tls) {
        if (!tls_ctx_.init(*opts_.tls)) {
            last_error_ = "TLS init failed: " + tls_ctx_.last_error();
            running_ = false;
            return false;
        }
        tls_enabled_ = true;
    }

    unsigned n = opts_.workers ? opts_.workers
                               : std::max(1u, std::thread::hardware_concurrency());

    std::string ip_str(ip);
    uint16_t bound = port;

    workers_.clear();
    for (unsigned i = 0; i < n; ++i) {
        auto w = std::make_unique<detail::Worker>(*this);
        std::string err;
        // First worker may bind port 0; the rest reuse the discovered port.
        if (!w->setup_listener(ip_str, bound, bound, err)) {
            last_error_ = err;
            for (auto& prev : workers_) prev->request_stop();
            workers_.clear();
            running_ = false;
            return false;
        }
        workers_.push_back(std::move(w));
    }
    port_ = bound;

    threads_.reserve(n);
    for (auto& w : workers_) {
        threads_.emplace_back([wp = w.get()] { wp->run(); });
    }
    return true;
}

void Server::Wait() {
    std::lock_guard<std::mutex> lk(join_mu_);
    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
}

void Server::Stop() {
    if (!running_.exchange(false)) return;
    for (auto& w : workers_) w->request_stop();
    Wait();
    threads_.clear();
    // Workers stay alive so late SSE handles can still post harmlessly;
    // they are destroyed with the Server.
}

} // namespace socketify
