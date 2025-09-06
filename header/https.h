// SPDX-License-Identifier: MIT
#pragma once
#include <filesystem>
#include <optional>
#include <string>

namespace socketify {

enum class TlsMode { Disabled, Enabled, Strict };

struct HttpsConfig {
    TlsMode mode{TlsMode::Disabled};
    std::filesystem::path cert_file;                 // PEM
    std::filesystem::path key_file;                  // PEM
    std::optional<std::filesystem::path> ca_file;    // optional chain
    std::optional<std::string> ciphers;              // OpenSSL cipher list
    bool http2{false};                               // future hook

    static HttpsConfig from_files(const std::filesystem::path& cert,
                                  const std::filesystem::path& key,
                                  std::optional<std::filesystem::path> ca = std::nullopt,
                                  TlsMode m = TlsMode::Enabled);

    // Reads: SOCKETIFY_TLS_CERT/KEY/CA/MODE/CIPHERS/HTTP2 (or custom prefix)
    static HttpsConfig from_env(const std::string& prefix = "SOCKETIFY_TLS_");
};

} // namespace socketify
