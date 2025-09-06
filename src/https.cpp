#include "https.h"
#include <cstdlib>

namespace socketify {

HttpsConfig HttpsConfig::from_files(const std::filesystem::path& cert,
                                    const std::filesystem::path& key,
                                    std::optional<std::filesystem::path> ca,
                                    TlsMode m) {
    HttpsConfig cfg;
    cfg.mode = m;
    cfg.cert_file = cert;
    cfg.key_file  = key;
    cfg.ca_file   = ca;
    return cfg;
}

HttpsConfig HttpsConfig::from_env(const std::string& prefix) {
    auto getenv_s = [&](const char* name) -> std::optional<std::string> {
        if (const char* v = std::getenv(name)) return std::string(v);
        return std::nullopt;
    };

    HttpsConfig cfg;
    if (auto v = getenv_s((prefix + "CERT").c_str())) cfg.cert_file = *v;
    if (auto v = getenv_s((prefix + "KEY").c_str()))  cfg.key_file  = *v;
    if (auto v = getenv_s((prefix + "CA").c_str()))   cfg.ca_file   = *v;
    if (auto v = getenv_s((prefix + "CIPHERS").c_str())) cfg.ciphers = *v;
    if (auto v = getenv_s((prefix + "HTTP2").c_str())) cfg.http2 = (*v == "1");

    if (auto v = getenv_s((prefix + "MODE").c_str())) {
        if (*v == "Disabled") cfg.mode = TlsMode::Disabled;
        else if (*v == "Strict") cfg.mode = TlsMode::Strict;
        else cfg.mode = TlsMode::Enabled;
    }
    return cfg;
}

} // namespace socketify
