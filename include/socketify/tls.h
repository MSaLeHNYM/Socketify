#pragma once
/**
 * @file tls.h
 * @brief HTTPS support: TLS options and the OpenSSL server context.
 *
 * Enable TLS by filling ServerOptions::tls:
 * @code
 * ServerOptions opts;
 * opts.tls = TlsOptions{.cert_file = "server.crt", .key_file = "server.key"};
 * Server server(opts);
 * server.Run("0.0.0.0", 443);
 * @endcode
 *
 * Certificate paths may also come from the environment variables
 * SOCKETIFY_CERT_FILE and SOCKETIFY_KEY_FILE (see TlsOptions::from_env()).
 *
 * Requires the library to be built with SOCKETIFY_WITH_TLS=ON.
 */

#include <memory>
#include <optional>
#include <string>

typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_st SSL;

namespace socketify {

/** @brief TLS listener configuration. */
struct TlsOptions {
    /** @brief Path to the PEM certificate (chain) file. */
    std::string cert_file;
    /** @brief Path to the PEM private key file. */
    std::string key_file;
    /**
     * @brief Minimum protocol version. Supported: "TLSv1.2" (default),
     *        "TLSv1.3".
     */
    std::string min_version{"TLSv1.2"};
    /** @brief Optional cipher list override (OpenSSL syntax). */
    std::string cipher_list;

    /**
     * @brief Build options from SOCKETIFY_CERT_FILE / SOCKETIFY_KEY_FILE.
     * @return std::nullopt when either variable is missing.
     */
    static std::optional<TlsOptions> from_env();
};

namespace tls {

/**
 * @brief Owns an OpenSSL SSL_CTX configured for server use.
 *
 * Created once per Server; each accepted connection gets a fresh SSL
 * session via new_session().
 */
class TlsContext {
public:
    TlsContext() = default;
    ~TlsContext();

    TlsContext(const TlsContext&) = delete;
    TlsContext& operator=(const TlsContext&) = delete;

    /**
     * @brief Load certificates and initialize the context.
     * @return false on any OpenSSL failure (details via last_error()).
     */
    bool init(const TlsOptions& opts);

    /** @brief True after a successful init(). */
    bool valid() const noexcept { return ctx_ != nullptr; }

    /**
     * @brief Create a server-side SSL session bound to @p fd.
     * @return New SSL object (caller owns; freed by detail::Socket), or
     *         nullptr on failure.
     */
    SSL* new_session(int fd) const;

    /** @brief Description of the most recent failure. */
    const std::string& last_error() const noexcept { return last_error_; }

private:
    SSL_CTX* ctx_{nullptr};
    std::string last_error_;
};

} // namespace tls

} // namespace socketify
