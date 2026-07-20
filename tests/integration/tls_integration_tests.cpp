// Integration tests for HTTPS: generate a self-signed cert at test time and
// exercise the TLS handshake + a request via the openssl CLI-independent
// OpenSSL client API.

#include "socketify/socketify.h"

#include <gtest/gtest.h>

#if defined(SOCKETIFY_HAS_TLS) && SOCKETIFY_HAS_TLS

#include <openssl/ssl.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace socketify;
namespace fs = std::filesystem;

namespace {

// Generate a throwaway self-signed cert via the openssl CLI.
bool generate_self_signed(const fs::path& cert, const fs::path& key) {
    std::string cmd =
        "openssl req -x509 -newkey rsa:2048 -nodes -batch "
        "-keyout " + key.string() + " -out " + cert.string() +
        " -days 1 -subj /CN=localhost >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

/// Minimal blocking TLS client for tests.
class TlsClient {
public:
    ~TlsClient() {
        if (ssl_) {
            SSL_shutdown(ssl_);
            SSL_free(ssl_);
        }
        if (ctx_) SSL_CTX_free(ctx_);
        if (fd_ >= 0) ::close(fd_);
    }

    bool connect_to(uint16_t port) {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return false;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;

        ctx_ = SSL_CTX_new(TLS_client_method());
        if (!ctx_) return false;
        // Self-signed test cert: skip verification.
        SSL_CTX_set_verify(ctx_, SSL_VERIFY_NONE, nullptr);
        ssl_ = SSL_new(ctx_);
        SSL_set_fd(ssl_, fd_);
        return SSL_connect(ssl_) == 1;
    }

    bool send_all(std::string_view data) {
        std::size_t off = 0;
        while (off < data.size()) {
            int n = SSL_write(ssl_, data.data() + off, static_cast<int>(data.size() - off));
            if (n <= 0) return false;
            off += static_cast<std::size_t>(n);
        }
        return true;
    }

    std::string read_some(std::size_t max = 65536) {
        std::string out;
        char buf[4096];
        while (out.size() < max) {
            int n = SSL_read(ssl_, buf, sizeof(buf));
            if (n <= 0) break;
            out.append(buf, static_cast<std::size_t>(n));
            if (out.find("\r\n\r\n") != std::string::npos &&
                out.find("Content-Length") != std::string::npos) {
                // Naive framing is fine for these small test responses.
                auto hdr_end = out.find("\r\n\r\n");
                auto cl_pos = out.find("Content-Length:");
                std::size_t cl = static_cast<std::size_t>(
                    std::atoll(out.c_str() + cl_pos + 15));
                if (out.size() >= hdr_end + 4 + cl) break;
            }
        }
        return out;
    }

    const char* tls_version() const { return SSL_get_version(ssl_); }

private:
    int fd_{-1};
    SSL_CTX* ctx_{nullptr};
    SSL* ssl_{nullptr};
};

} // namespace

class TlsTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() / ("socketify_tls_" + std::to_string(::getpid()));
        fs::create_directories(dir_);
        cert_ = dir_ / "server.crt";
        key_ = dir_ / "server.key";
        if (!generate_self_signed(cert_, key_)) {
            GTEST_SKIP() << "openssl CLI not available for cert generation";
        }

        ServerOptions opts;
        opts.tls = TlsOptions{.cert_file = cert_.string(), .key_file = key_.string()};
        server_ = std::make_unique<Server>(opts);
        server_->Get("/secure", [](Request&, Response& res) { res.send("tls ok"); });
        ASSERT_TRUE(server_->Run("127.0.0.1", 0)) << server_->last_error();
        port_ = server_->port();
    }

    void TearDown() override {
        if (server_) server_->Stop();
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    fs::path dir_, cert_, key_;
    std::unique_ptr<Server> server_;
    uint16_t port_{0};
};

TEST_F(TlsTest, HandshakeAndRequest) {
    TlsClient c;
    ASSERT_TRUE(c.connect_to(port_));
    ASSERT_TRUE(c.send_all("GET /secure HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n"));
    std::string resp = c.read_some();
    EXPECT_NE(resp.find("200 OK"), std::string::npos);
    EXPECT_NE(resp.find("tls ok"), std::string::npos);
}

TEST_F(TlsTest, NegotiatesModernTls) {
    TlsClient c;
    ASSERT_TRUE(c.connect_to(port_));
    std::string v = c.tls_version();
    EXPECT_TRUE(v == "TLSv1.2" || v == "TLSv1.3") << v;
}

TEST(TlsConfig, InitFailsWithMissingFiles) {
    ServerOptions opts;
    opts.tls = TlsOptions{.cert_file = "/nonexistent.crt", .key_file = "/nonexistent.key"};
    Server server(opts);
    EXPECT_FALSE(server.Run("127.0.0.1", 0));
    EXPECT_FALSE(server.last_error().empty());
}

#else

TEST(TlsConfig, DisabledBuildFailsGracefully) {
    using namespace socketify;
    ServerOptions opts;
    opts.tls = TlsOptions{.cert_file = "x", .key_file = "y"};
    Server server(opts);
    EXPECT_FALSE(server.Run("127.0.0.1", 0));
}

#endif // SOCKETIFY_HAS_TLS
