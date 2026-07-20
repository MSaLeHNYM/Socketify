#pragma once
/**
 * @file socket.h
 * @brief Non-blocking socket wrapper unifying plain TCP and TLS I/O.
 *
 * All reads/writes are non-blocking and report readiness requirements via
 * IoResult, so the event loop can subscribe to EPOLLIN/EPOLLOUT correctly
 * for both plain and TLS connections.
 */

#include <cstddef>
#include <cstdint>
#include <string>

// Forward-declare OpenSSL types so this header does not require openssl headers.
typedef struct ssl_st SSL;

namespace socketify::detail {

/** @brief Outcome of a non-blocking I/O operation. */
enum class IoResult {
    Ok,        ///< Some bytes were transferred (see the out parameter).
    WantRead,  ///< Retry when the fd becomes readable.
    WantWrite, ///< Retry when the fd becomes writable.
    Closed,    ///< Peer performed an orderly shutdown.
    Error      ///< Unrecoverable error; close the connection.
};

/**
 * @brief RAII non-blocking connection socket (plain TCP or TLS).
 *
 * Ownership: the Socket owns the fd and the SSL object (when TLS) and
 * closes/frees both on destruction.
 */
class Socket {
public:
    Socket() = default;
    /** @brief Adopt an accepted fd. The fd is switched to non-blocking mode. */
    explicit Socket(int fd);
    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    /** @brief Underlying file descriptor (-1 when closed). */
    int fd() const noexcept { return fd_; }
    /** @brief True while the fd is open. */
    bool valid() const noexcept { return fd_ >= 0; }

    /**
     * @brief Attach a TLS session (server side). The socket takes ownership
     *        of @p ssl and will perform the handshake lazily via handshake().
     */
    void adopt_tls(SSL* ssl) noexcept { ssl_ = ssl; handshaken_ = false; }

    /** @brief True when this socket carries TLS. */
    bool is_tls() const noexcept { return ssl_ != nullptr; }

    /** @brief True once the TLS handshake finished (always true for plain). */
    bool handshake_done() const noexcept { return ssl_ == nullptr || handshaken_; }

    /**
     * @brief Progress the TLS handshake (no-op for plain sockets).
     * @return Ok when complete; WantRead/WantWrite to wait for readiness.
     */
    IoResult handshake();

    /**
     * @brief Read up to @p len bytes into @p buf.
     * @param[out] out Number of bytes read on Ok.
     */
    IoResult read(char* buf, std::size_t len, std::size_t& out);

    /**
     * @brief Write up to @p len bytes from @p buf.
     * @param[out] out Number of bytes written on Ok (may be a short write).
     */
    IoResult write(const char* buf, std::size_t len, std::size_t& out);

    /**
     * @brief Zero-copy file transmission (sendfile(2) on plain sockets;
     *        read+write fallback under TLS).
     * @param file_fd  Source file descriptor.
     * @param offset   In/out file offset, advanced by the bytes sent.
     * @param len      Maximum number of bytes to send.
     * @param[out] out Bytes actually sent on Ok.
     */
    IoResult send_file(int file_fd, std::uint64_t& offset, std::size_t len, std::size_t& out);

    /** @brief Close the socket (and free the TLS session). Idempotent. */
    void close() noexcept;

    /** @brief Set/get the peer address (filled by the acceptor). */
    void set_remote_ip(std::string ip) { remote_ip_ = std::move(ip); }
    const std::string& remote_ip() const noexcept { return remote_ip_; }

    /** @brief Make an fd non-blocking; returns false on fcntl failure. */
    static bool set_nonblocking(int fd) noexcept;
    /** @brief Set FD_CLOEXEC on @p fd. */
    static bool set_cloexec(int fd) noexcept;
    /** @brief Disable Nagle's algorithm on @p fd. */
    static bool set_nodelay(int fd) noexcept;

private:
    IoResult map_ssl_error_(int ret);

    int fd_{-1};
    SSL* ssl_{nullptr};
    bool handshaken_{false};
    std::string remote_ip_;
};

} // namespace socketify::detail
