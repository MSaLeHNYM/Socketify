// SPDX-License-Identifier: MIT
#pragma once
#include "http.h"
#include "request.h"
#include "response.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace socketify
{

    // Handlers & middleware
    using Handler = std::function<void(Request &, Response &)>;
    using Next = std::function<void()>;
    using Middleware = std::function<void(Request &, Response &, Next)>;

    // TLS/SSL configuration (can come from files or env)
    enum class TlsMode
    {
        Disabled,
        Enabled,
        Strict
    };

    struct TlsConfig
    {
        TlsMode mode{TlsMode::Enabled};
        std::filesystem::path cert_file;              // PEM
        std::filesystem::path key_file;               // PEM
        std::optional<std::filesystem::path> ca_file; // optional CA chain
        std::optional<std::string> ciphers;           // OpenSSL cipher list
        bool http2{false};                            // hook for future

        static TlsConfig from_files(const std::filesystem::path &cert,
                                    const std::filesystem::path &key,
                                    std::optional<std::filesystem::path> ca = std::nullopt,
                                    TlsMode mode = TlsMode::Enabled);

        // Reads env vars (prefix defaults to SOCKETIFY_TLS_)
        // ${PREFIX}CERT, KEY, CA, MODE(Disabled|Enabled|Strict),
        // CIPHERS, HTTP2(0|1)
        static TlsConfig from_env(const std::string &prefix = "SOCKETIFY_TLS_");
    };

    // Limits & options
    struct BodyLimits
    {
        std::size_t json = 2 * 1024 * 1024; // 2MB
        std::size_t form = 2 * 1024 * 1024;
        std::size_t multipart_total = 50 * 1024 * 1024; // 50MB
        std::size_t file_part = 8 * 1024 * 1024;        // per uploaded part
    };

    struct CompressionOptions
    {
        bool enable{true};
        int level{-1}; // impl-defined default
    };

    struct CorsOptions
    {
        bool enable{true};
        std::vector<std::string> allow_origins{"*"};
        std::vector<std::string> allow_methods{"GET", "POST", "PUT", "PATCH", "DELETE", "OPTIONS"};
        std::vector<std::string> allow_headers{"Content-Type", "Authorization"};
        bool allow_credentials{false};
        int max_age_seconds{600};
    };

    struct RateLimitOptions
    {
        bool enable{false};
        int requests{100};
        std::chrono::seconds window{60};
    };

    struct SessionOptions
    {
        bool enable{false};
        std::string cookie_name{"sid"};
        std::string secret; // HMAC key
        std::chrono::seconds ttl{std::chrono::hours{24}};
        bool secure{true};
        bool http_only{true};
        std::string same_site{"Lax"};
        // backend hook (in-memory, redis, etc.) in impl
    };

    struct LoggerOptions
    {
        bool access_log{true};
        bool json{false};
        std::string level{"info"}; // info|debug|warn|error
    };

    struct StaticOptions
    {
        std::filesystem::path root{};
        bool fallthrough{true};
        bool etag{true};
        bool caching{true};
        int max_age_seconds{3600};
        std::string url_prefix{"/static"};
    };

    class Server
    {
    public:
        struct Options
        {
            int threads{0}; // 0 => hardware_concurrency
            BodyLimits body_limits{};
            CompressionOptions compression{};
            CorsOptions cors{};
            RateLimitOptions rate_limit{};
            SessionOptions sessions{};
            LoggerOptions logger{};
            StaticOptions static_files{};
            bool trust_proxy{false};
            std::optional<TlsConfig> tls{};
            std::optional<std::string> metrics_path{std::string{"/metrics"}};
            std::optional<std::string> health_path{std::string{"/healthz"}};
            bool dev_auto_reload{false};
        };

        Server(); // default
        explicit Server(const Options &opts);
        ~Server();

        // Middleware
        Server &use(Middleware mw);
        Server &use_before_routing(Middleware mw);
        Server &use_after_routing(Middleware mw);

        // Routing
        Server &route(HttpMethod m, std::string_view pattern, Handler h);
        Server &get(std::string_view pattern, Handler h);
        Server &post(std::string_view pattern, Handler h);
        Server &put(std::string_view pattern, Handler h);
        Server &patch(std::string_view pattern, Handler h);
        Server &del(std::string_view pattern, Handler h);
        Server &options(std::string_view pattern, Handler h);
        Server &head(std::string_view pattern, Handler h);

        // Route groups / sub-routers
        Server &group(std::string_view base, const std::function<void(Server &)> &builder);

        // Static files
        Server &static_dir(const StaticOptions &opt);

        // Convenience toggles
        Server &enable_cors(const CorsOptions &opt);
        Server &enable_compression(const CompressionOptions &opt);
        Server &enable_rate_limit(const RateLimitOptions &opt);
        Server &enable_sessions(const SessionOptions &opt);

        // TLS control
        Server &enable_tls(const TlsConfig &tls);
        Server &enable_tls_from_env(const std::string &prefix = "SOCKETIFY_TLS_");

        // Observability
        Server &with_metrics(std::string_view path = "/metrics");
        Server &with_healthcheck(std::string_view path = "/healthz",
                                 std::function<bool()> check = nullptr);

        // Background tasks
        using Job = std::function<void()>;
        Server &schedule_cron(std::string_view spec, Job job); // "*/5 * * * *"
        Server &schedule_every(std::chrono::milliseconds, Job job);

        // Lifecycle
        void listen(std::string_view host, uint16_t port);
        void listen_tls(std::string_view host, uint16_t port, const TlsConfig &tls);
        void run();                 // blocking
        void shutdown_gracefully(); // SIGINT/SIGTERM handler should call this

        // Plugins
        template <typename T>
        Server &register_plugin(std::string_view name, std::shared_ptr<T> plugin);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace socketify
