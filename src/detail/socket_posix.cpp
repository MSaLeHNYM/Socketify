/**
 * @file socket_posix.cpp
 * @brief POSIX (+ optional OpenSSL) implementation of detail::Socket.
 */

#include "socketify/detail/socket.h"

#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <unistd.h>

#if defined(SOCKETIFY_HAS_TLS) && SOCKETIFY_HAS_TLS
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

namespace socketify::detail {

namespace {
inline bool would_block_(int e) noexcept {
#if EWOULDBLOCK != EAGAIN
    if (e == EWOULDBLOCK) return true;
#endif
    return e == EAGAIN;
}
} // namespace

Socket::Socket(int fd) : fd_(fd) {
    if (fd_ >= 0) {
        set_nonblocking(fd_);
        set_cloexec(fd_);
        set_nodelay(fd_);
    }
}

Socket::~Socket() { close(); }

Socket::Socket(Socket&& other) noexcept
    : fd_(other.fd_), ssl_(other.ssl_), handshaken_(other.handshaken_),
      remote_ip_(std::move(other.remote_ip_)) {
    other.fd_ = -1;
    other.ssl_ = nullptr;
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        ssl_ = other.ssl_;
        handshaken_ = other.handshaken_;
        remote_ip_ = std::move(other.remote_ip_);
        other.fd_ = -1;
        other.ssl_ = nullptr;
    }
    return *this;
}

void Socket::close() noexcept {
#if defined(SOCKETIFY_HAS_TLS) && SOCKETIFY_HAS_TLS
    if (ssl_) {
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
#endif
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool Socket::set_nonblocking(int fd) noexcept {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool Socket::set_cloexec(int fd) noexcept {
    int flags = ::fcntl(fd, F_GETFD, 0);
    if (flags < 0) return false;
    return ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}

bool Socket::set_nodelay(int fd) noexcept {
    int yes = 1;
    return ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) == 0;
}

#if defined(SOCKETIFY_HAS_TLS) && SOCKETIFY_HAS_TLS
IoResult Socket::map_ssl_error_(int ret) {
    int err = SSL_get_error(ssl_, ret);
    switch (err) {
        case SSL_ERROR_WANT_READ: return IoResult::WantRead;
        case SSL_ERROR_WANT_WRITE: return IoResult::WantWrite;
        case SSL_ERROR_ZERO_RETURN: return IoResult::Closed;
        case SSL_ERROR_SYSCALL:
            if (ret == 0) return IoResult::Closed;
            if (would_block_(errno)) return IoResult::WantRead;
            return IoResult::Error;
        default:
            ERR_clear_error();
            return IoResult::Error;
    }
}
#else
IoResult Socket::map_ssl_error_(int) { return IoResult::Error; }
#endif

IoResult Socket::handshake() {
    if (!ssl_) return IoResult::Ok;
#if defined(SOCKETIFY_HAS_TLS) && SOCKETIFY_HAS_TLS
    if (handshaken_) return IoResult::Ok;
    int rc = SSL_accept(ssl_);
    if (rc == 1) {
        handshaken_ = true;
        return IoResult::Ok;
    }
    return map_ssl_error_(rc);
#else
    return IoResult::Error;
#endif
}

IoResult Socket::read(char* buf, std::size_t len, std::size_t& out) {
    out = 0;
    if (fd_ < 0) return IoResult::Error;
#if defined(SOCKETIFY_HAS_TLS) && SOCKETIFY_HAS_TLS
    if (ssl_) {
        int rc = SSL_read(ssl_, buf, static_cast<int>(len));
        if (rc > 0) {
            out = static_cast<std::size_t>(rc);
            return IoResult::Ok;
        }
        return map_ssl_error_(rc);
    }
#endif
    ssize_t rc = ::recv(fd_, buf, len, 0);
    if (rc > 0) {
        out = static_cast<std::size_t>(rc);
        return IoResult::Ok;
    }
    if (rc == 0) return IoResult::Closed;
    if (would_block_(errno) || errno == EINTR) return IoResult::WantRead;
    return IoResult::Error;
}

IoResult Socket::write(const char* buf, std::size_t len, std::size_t& out) {
    out = 0;
    if (fd_ < 0) return IoResult::Error;
#if defined(SOCKETIFY_HAS_TLS) && SOCKETIFY_HAS_TLS
    if (ssl_) {
        int rc = SSL_write(ssl_, buf, static_cast<int>(len));
        if (rc > 0) {
            out = static_cast<std::size_t>(rc);
            return IoResult::Ok;
        }
        return map_ssl_error_(rc);
    }
#endif
    ssize_t rc = ::send(fd_, buf, len, MSG_NOSIGNAL);
    if (rc > 0) {
        out = static_cast<std::size_t>(rc);
        return IoResult::Ok;
    }
    if (rc == 0) return IoResult::WantWrite;
    if (would_block_(errno) || errno == EINTR) return IoResult::WantWrite;
    if (errno == EPIPE || errno == ECONNRESET) return IoResult::Closed;
    return IoResult::Error;
}

IoResult Socket::send_file(int file_fd, std::uint64_t& offset, std::size_t len, std::size_t& out) {
    out = 0;
    if (fd_ < 0) return IoResult::Error;

    if (!is_tls()) {
        off_t off = static_cast<off_t>(offset);
        ssize_t rc = ::sendfile(fd_, file_fd, &off, len);
        if (rc > 0) {
            offset = static_cast<std::uint64_t>(off);
            out = static_cast<std::size_t>(rc);
            return IoResult::Ok;
        }
        if (rc == 0) return IoResult::Ok; // EOF on source
        if (would_block_(errno) || errno == EINTR) return IoResult::WantWrite;
        if (errno == EPIPE || errno == ECONNRESET) return IoResult::Closed;
        return IoResult::Error;
    }

    // TLS cannot use sendfile: read a chunk and push it through SSL_write.
    char chunk[64 * 1024];
    std::size_t want = len < sizeof(chunk) ? len : sizeof(chunk);
    ssize_t rd = ::pread(file_fd, chunk, want, static_cast<off_t>(offset));
    if (rd < 0) return IoResult::Error;
    if (rd == 0) return IoResult::Ok;
    std::size_t written = 0;
    IoResult r = write(chunk, static_cast<std::size_t>(rd), written);
    if (r == IoResult::Ok) {
        offset += written;
        out = written;
    }
    return r;
}

} // namespace socketify::detail
