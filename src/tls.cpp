/**
 * @file tls.cpp
 * @brief OpenSSL-backed TlsContext implementation.
 */

#include "socketify/tls.h"

#include <cstdlib>

#if defined(SOCKETIFY_HAS_TLS) && SOCKETIFY_HAS_TLS
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

namespace socketify {

std::optional<TlsOptions> TlsOptions::from_env() {
    const char* cert = std::getenv("SOCKETIFY_CERT_FILE");
    const char* key = std::getenv("SOCKETIFY_KEY_FILE");
    if (!cert || !key || !*cert || !*key) return std::nullopt;
    TlsOptions o;
    o.cert_file = cert;
    o.key_file = key;
    return o;
}

namespace tls {

#if defined(SOCKETIFY_HAS_TLS) && SOCKETIFY_HAS_TLS

static std::string collect_openssl_errors_() {
    std::string out;
    unsigned long e;
    char buf[256];
    while ((e = ERR_get_error()) != 0) {
        ERR_error_string_n(e, buf, sizeof(buf));
        if (!out.empty()) out.append("; ");
        out.append(buf);
    }
    if (out.empty()) out = "unknown OpenSSL error";
    return out;
}

TlsContext::~TlsContext() {
    if (ctx_) SSL_CTX_free(ctx_);
}

bool TlsContext::init(const TlsOptions& opts) {
    if (ctx_) {
        SSL_CTX_free(ctx_);
        ctx_ = nullptr;
    }

    ctx_ = SSL_CTX_new(TLS_server_method());
    if (!ctx_) {
        last_error_ = collect_openssl_errors_();
        return false;
    }

    int min_ver = TLS1_2_VERSION;
    if (opts.min_version == "TLSv1.3") min_ver = TLS1_3_VERSION;
    SSL_CTX_set_min_proto_version(ctx_, min_ver);

    SSL_CTX_set_options(ctx_, SSL_OP_NO_COMPRESSION | SSL_OP_CIPHER_SERVER_PREFERENCE);
    SSL_CTX_set_mode(ctx_, SSL_MODE_ENABLE_PARTIAL_WRITE |
                               SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

    if (!opts.cipher_list.empty() &&
        SSL_CTX_set_cipher_list(ctx_, opts.cipher_list.c_str()) != 1) {
        last_error_ = collect_openssl_errors_();
        SSL_CTX_free(ctx_);
        ctx_ = nullptr;
        return false;
    }

    if (SSL_CTX_use_certificate_chain_file(ctx_, opts.cert_file.c_str()) != 1 ||
        SSL_CTX_use_PrivateKey_file(ctx_, opts.key_file.c_str(), SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_check_private_key(ctx_) != 1) {
        last_error_ = collect_openssl_errors_();
        SSL_CTX_free(ctx_);
        ctx_ = nullptr;
        return false;
    }

    return true;
}

SSL* TlsContext::new_session(int fd) const {
    if (!ctx_) return nullptr;
    SSL* ssl = SSL_new(ctx_);
    if (!ssl) return nullptr;
    if (SSL_set_fd(ssl, fd) != 1) {
        SSL_free(ssl);
        return nullptr;
    }
    SSL_set_accept_state(ssl);
    return ssl;
}

#else // !SOCKETIFY_HAS_TLS

TlsContext::~TlsContext() = default;

bool TlsContext::init(const TlsOptions&) {
    last_error_ = "Socketify was built without TLS support (SOCKETIFY_WITH_TLS=OFF)";
    return false;
}

SSL* TlsContext::new_session(int) const { return nullptr; }

#endif

} // namespace tls

} // namespace socketify
