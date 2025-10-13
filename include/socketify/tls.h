#pragma once
// socketify/tls.h — TLS/SSL support options and context

#include <memory>
#include <string>

// Forward declaration for SSL context (e.g., from OpenSSL)
// This avoids including heavy SSL headers in our public API.
struct ssl_ctx_st;

namespace socketify {

namespace tls {

// Options for configuring TLS
struct Options {
    // Path to the server's certificate file (PEM format).
    std::string cert_file_path;
    // Path to the server's private key file (PEM format).
    std::string key_file_path;
    // Optional: Password for the private key.
    std::string key_password;
    // Optional: Path to a Diffie-Hellman parameters file.
    std::string dh_params_file_path;
};

// Represents a configured TLS context.
// This is a wrapper around the underlying SSL library's context (e.g., OpenSSL).
class Context {
public:
    // Creates a TLS context from the given options.
    // Throws a runtime_error on failure (e.g., file not found, bad format).
    explicit Context(const Options& opts);
    ~Context();

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    // Get the underlying SSL_CTX* (for internal use).
    ssl_ctx_st* get() const { return ssl_ctx_.get(); }

private:
    // Using a unique_ptr with a custom deleter for the SSL_CTX.
    struct SSLContextDeleter {
        void operator()(ssl_ctx_st* ctx) const;
    };
    std::unique_ptr<ssl_ctx_st, SSLContextDeleter> ssl_ctx_;
};

} // namespace tls

} // namespace socketify