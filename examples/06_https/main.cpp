// 06_https — TLS with a self-signed development certificate.
//
// Generate a cert first (helper script is copied next to the binary):
//   ./gen_cert.sh            # writes server.crt + server.key
//   ./example_06_https
//
// Try it:
//   curl -k https://localhost:8443/
//
// Certificate paths can also come from the environment:
//   SOCKETIFY_CERT_FILE=server.crt SOCKETIFY_KEY_FILE=server.key ./example_06_https

#include <socketify/socketify.h>

#include <cstdio>

using namespace socketify;

int main() {
    ServerOptions opts;

    // Prefer env vars (SOCKETIFY_CERT_FILE / SOCKETIFY_KEY_FILE), fall back
    // to files in the working directory.
    if (auto env = TlsOptions::from_env()) {
        opts.tls = *env;
    } else {
        opts.tls = TlsOptions{.cert_file = "server.crt", .key_file = "server.key"};
    }

    Server server(opts);

    server.Get("/", [](Request& req, Response& res) {
        res.json({{"secure", true}, {"client", std::string(req.remote_ip())}});
    });

    if (!server.Run("0.0.0.0", 8443)) {
        std::fprintf(stderr, "failed to start: %s\n", server.last_error().c_str());
        std::fprintf(stderr, "hint: run ./gen_cert.sh first to create server.crt/server.key\n");
        return 1;
    }
    std::printf("HTTPS on https://localhost:8443 (curl -k to accept the self-signed cert)\n");
    server.Wait();
    return 0;
}
