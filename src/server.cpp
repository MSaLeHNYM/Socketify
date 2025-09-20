#include "socketify/server.h"

#include "socketify/http.h"
#include "socketify/request.h"
#include "socketify/response.h"
#include "socketify/detail/http_parser.h"
#include "socketify/compression.h" // NEW

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

namespace socketify
{

    Server::Server(ServerOptions opts)
        : opts_(std::move(opts)) {}

    Server::~Server() { Stop(); }

    bool Server::Run(std::string_view ip, uint16_t port)
    {
        if (running_)
            return true;

        std::string err;
        listen_fd_ = create_listen_socket_(ip, port, opts_.backlog, err);
        if (listen_fd_ < 0)
        {
            if (on_error_)
                on_error_(err);
            return false;
        }
        running_ = true;
        accept_thread_ = std::thread(&Server::accept_loop_, this);
        return true;
    }

    void Server::Stop()
    {
        bool expected = true;
        if (!running_.compare_exchange_strong(expected, false))
            return;
        if (listen_fd_ >= 0)
        {
            ::shutdown(listen_fd_, SHUT_RDWR);
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
        if (accept_thread_.joinable())
            accept_thread_.join();
    }

    void Server::accept_loop_()
    {
        while (running_)
        {
            sockaddr_in addr{};
            socklen_t len = sizeof(addr);
            int fd = ::accept(listen_fd_, reinterpret_cast<sockaddr *>(&addr), &len);
            if (fd < 0)
            {
                if (!running_)
                    break;
                if (errno == EINTR)
                    continue;
                if (on_error_)
                    on_error_(std::string("accept failed: ") + std::strerror(errno));
                continue;
            }
            std::thread(&Server::handle_connection_, this, fd).detach();
        }
    }

    void Server::handle_connection_(int client_fd)
    {
        bool keep_alive = true;

        while (keep_alive)
        {
            detail::HttpParser parser;
            Request req;
            Response res;

            char buf[8192];

            {
                timeval tv{};
                tv.tv_sec = opts_.read_header_timeout_ms / 1000;
                tv.tv_usec = (opts_.read_header_timeout_ms % 1000) * 1000;
                ::setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            }

            while (!parser.complete() && !parser.error())
            {
                ssize_t n = ::recv(client_fd, buf, sizeof(buf), 0);
                if (n == 0)
                {
                    keep_alive = false;
                    break;
                }
                if (n < 0)
                {
                    if (errno == EINTR)
                        continue;
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        keep_alive = false;
                        break;
                    }
                    if (on_error_)
                        on_error_(std::string("recv failed: ") + std::strerror(errno));
                    keep_alive = false;
                    break;
                }
                std::size_t consumed = parser.consume(buf, static_cast<std::size_t>(n));
                if (consumed == 0 && n > 0 && !parser.complete() && !parser.error())
                {
                    keep_alive = false;
                    break;
                }

                if (parser.state() == detail::ParseState::Body)
                {
                    timeval tv{};
                    tv.tv_sec = opts_.read_body_timeout_ms / 1000;
                    tv.tv_usec = (opts_.read_body_timeout_ms % 1000) * 1000;
                    ::setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                }
            }

            if (!parser.complete())
            {
                if (parser.error())
                {
                    res.status(Status::BadRequest).send("Bad Request\n");
                    auto out = serialize_response_(req, res, opts_);
                    write_all_(client_fd, out.data(), out.size());
                }
                break;
            }

            // Populate Request from parser
            req.set_method(parser.method());
            req.set_target(std::string(parser.target()));
            req.set_path(std::string(parser.path()));
            req.set_version(std::string(parser.version()));
            auto &hdrs = req.mutable_headers();
            for (const auto &kv : parser.headers())
                hdrs.insert(kv);
            if (!parser.body_view().empty())
                req.set_body_storage(std::string(parser.body_view()));

            // Dispatch
            bool matched = router_.dispatch(req, res);
            if (!matched && !res.ended())
            {
                res.status(Status::NotFound).send("Not Found\n");
            }
            if (!res.ended())
                res.end();

            // Serialize (with compression if acceptable)
            auto out = serialize_response_(req, res, opts_);
            if (!write_all_(client_fd, out.data(), out.size()))
            {
                keep_alive = false;
            }

            keep_alive = !should_close_(req, res);
            if (!keep_alive)
                break;

            {
                timeval tv{};
                tv.tv_sec = opts_.idle_timeout_ms / 1000;
                tv.tv_usec = (opts_.idle_timeout_ms % 1000) * 1000;
                ::setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            }
        }

        ::shutdown(client_fd, SHUT_RDWR);
        ::close(client_fd);
    }

    int Server::create_listen_socket_(std::string_view ip, uint16_t port, int backlog, std::string &err)
    {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
        {
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
        if (ip == "0.0.0.0" || ip.empty())
        {
            addr.sin_addr.s_addr = INADDR_ANY;
        }
        else if (::inet_pton(AF_INET, std::string(ip).c_str(), &addr.sin_addr) != 1)
        {
            err = "inet_pton failed for IP: " + std::string(ip);
            ::close(fd);
            return -1;
        }

        if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
        {
            err = std::string("bind() failed: ") + std::strerror(errno);
            ::close(fd);
            return -1;
        }
        if (::listen(fd, backlog) < 0)
        {
            err = std::string("listen() failed: ") + std::strerror(errno);
            ::close(fd);
            return -1;
        }

        return fd;
    }

    bool Server::write_all_(int fd, const void *buf, size_t len)
    {
        const char *p = static_cast<const char *>(buf);
        size_t off = 0;
        while (off < len)
        {
            ssize_t n = ::send(fd, p + off, len - off, 0);
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                return false;
            }
            if (n == 0)
                return false;
            off += static_cast<size_t>(n);
        }
        return true;
    }

    std::string Server::make_date_header_()
    {
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

    std::string Server::serialize_response_(const Request &req, const Response &res, const ServerOptions &opts)
    {
        // Copy body to a working buffer (may compress)
        std::string body = res.body_string();

        // Decide whether to compress
        bool can_compress = false;
        compression::Encoding enc = compression::Encoding::None;

        auto find_header = [](const HeaderMap &h, std::string_view key) -> std::string_view
        {
            auto it = h.find(std::string(key));
            if (it == h.end())
                return {};
            return it->second;
        };

        if (opts.compression.enable && res.has_body() && find_header(res.headers(), "Content-Encoding").empty())
        {
            const auto ae = find_header(req.headers(), "Accept-Encoding");
            enc = compression::negotiate_accept_encoding(ae, opts.compression);
            if (enc != compression::Encoding::None)
            {
                const auto ct = find_header(res.headers(), "Content-Type");
                if (compression::is_compressible_type(ct, opts.compression) &&
                    body.size() >= opts.compression.min_size)
                {
                    can_compress = true;
                }
            }
        }

        std::string compressed;
        std::string content_encoding_value;

        if (can_compress)
        {
            bool ok = false;
            if (enc == compression::Encoding::Gzip)
            {
                ok = compression::gzip_compress(body, compressed);
                content_encoding_value = "gzip";
            }
            else if (enc == compression::Encoding::Deflate)
            {
                ok = compression::deflate_compress(body, compressed);
                content_encoding_value = "deflate";
            }
            if (ok)
            {
                body.swap(compressed);
            }
            else
            {
                content_encoding_value.clear();
            }
        }

        // Build headers
        std::string out;
        out.reserve(256 + body.size());

        // Status line
        out += "HTTP/1.1 ";
        out += std::to_string(res.status_code());
        out.push_back(' ');
        out += std::string(reason(static_cast<Status>(res.status_code())));
        out += "\r\n";

        // Common headers
        out += "Date: ";
        out += make_date_header_();
        out += "\r\n";
        out += "Server: socketify/0.1\r\n";

        // Emit user headers (except Content-Length; we’ll set it ourselves)
        bool have_vary = false;
        for (const auto &kv : res.headers())
        {
            std::string key = kv.first;
            std::string low = key;
            for (auto &c : low)
                if (c >= 'A' && c <= 'Z')
                    c = char(c - 'A' + 'a');

            if (low == "content-length")
                continue; // we always overwrite
            if (!content_encoding_value.empty() && low == "content-encoding")
            {
                // user explicitly set Content-Encoding → keep theirs and skip ours
                content_encoding_value.clear();
            }
            if (low == "vary")
                have_vary = true;

            out += key;
            out += ": ";
            out += kv.second;
            out += "\r\n";
        }

        // Our Content-Encoding (if we actually compressed)
        if (!content_encoding_value.empty())
        {
            out += "Content-Encoding: ";
            out += content_encoding_value;
            out += "\r\n";
            if (!have_vary)
            {
                out += "Vary: Accept-Encoding\r\n";
            }
        }

        // Content-Length
        out += "Content-Length: ";
        out += std::to_string(body.size());
        out += "\r\n";

        // End headers + body
        out += "\r\n";
        if (!body.empty())
            out += body;

        return out;
    }

    bool Server::should_close_(const Request &req, const Response &res)
    {
        auto find_header = [](const HeaderMap &h, std::string_view key) -> std::string_view
        {
            auto it = h.find(std::string(key));
            if (it == h.end())
                return {};
            return it->second;
        };

        auto rconn = find_header(req.headers(), H_Connection);
        if (!rconn.empty())
        {
            std::string v(rconn);
            for (auto &c : v)
                if (c >= 'A' && c <= 'Z')
                    c = char(c - 'A' + 'a');
            if (v.find("close") != std::string::npos)
                return true;
        }

        auto sconn = find_header(res.headers(), H_Connection);
        if (!sconn.empty())
        {
            std::string v(sconn);
            for (auto &c : v)
                if (c >= 'A' && c <= 'Z')
                    c = char(c - 'A' + 'a');
            if (v.find("close") != std::string::npos)
                return true;
            if (v.find("keep-alive") != std::string::npos)
                return false;
        }

        return false; // HTTP/1.1 default keep-alive
    }

} // namespace socketify
