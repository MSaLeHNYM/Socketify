// SPDX-License-Identifier: MIT
#include "server.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <regex>
#include <sstream>
#include <thread>

// If you have your own Request/Response implementations with body parsing,
// keep including their headers in server.h; this file won’t touch internals.

namespace socketify
{

    // ----------------------------- TLS helpers -----------------------------

    TlsConfig TlsConfig::from_files(const std::filesystem::path &cert,
                                    const std::filesystem::path &key,
                                    std::optional<std::filesystem::path> ca,
                                    TlsMode mode)
    {
        TlsConfig t;
        t.mode = mode;
        t.cert_file = cert;
        t.key_file = key;
        t.ca_file = std::move(ca);
        return t;
    }

    static inline std::optional<std::string> get_env(std::string k)
    {
#ifdef _WIN32
        // minimal cross-platform getenv wrapper
        const char *v = std::getenv(k.c_str());
        if (v)
            return std::string(v);
        return std::nullopt;
#else
        const char *v = std::getenv(k.c_str());
        if (v)
            return std::string(v);
        return std::nullopt;
#endif
    }

    TlsConfig TlsConfig::from_env(const std::string &prefix)
    {
        TlsConfig t;

        if (auto v = get_env(prefix + "CERT"))
            t.cert_file = *v;
        if (auto v = get_env(prefix + "KEY"))
            t.key_file = *v;
        if (auto v = get_env(prefix + "CA"))
            t.ca_file = std::filesystem::path(*v);
        if (auto v = get_env(prefix + "CIPHERS"))
            t.ciphers = *v;
        if (auto v = get_env(prefix + "HTTP2"))
            t.http2 = (*v == "1" || *v == "true" || *v == "TRUE");

        if (auto v = get_env(prefix + "MODE"))
        {
            if (*v == "Disabled" || *v == "disabled")
                t.mode = TlsMode::Disabled;
            else if (*v == "Strict" || *v == "strict")
                t.mode = TlsMode::Strict;
            else
                t.mode = TlsMode::Enabled;
        }
        else
        {
            // by default, if cert/key are present, enable TLS
            if (!t.cert_file.empty() && !t.key_file.empty())
                t.mode = TlsMode::Enabled;
            else
                t.mode = TlsMode::Disabled;
        }
        return t;
    }

    // ----------------------------- Server impl -----------------------------

    // Lightweight router here so server.cpp is self-contained.
    // If you already have header/route.h with a Router, you can swap
    // this out easily by delegating to it.

    struct CompiledRoute
    {
        HttpMethod method;
        std::string pattern;            // original pattern, e.g. "/users/:id"
        std::regex re;                  // compiled regex
        std::vector<std::string> names; // param names captured from pattern
        Handler handler;
    };

    static std::string escape_regex_char(char c)
    {
        static const std::string specials = R"(.^$|()[]{}*+?\)";
        if (specials.find(c) != std::string::npos)
        {
            return std::string("\\") + c;
        }
        return std::string(1, c);
    }

    static CompiledRoute compile_route(HttpMethod m, const std::string &pattern, Handler h)
    {
        std::string re_str = "^";
        std::vector<std::string> names;

        for (size_t i = 0; i < pattern.size();)
        {
            char ch = pattern[i];
            if (ch == ':')
            {
                // parse name
                size_t j = i + 1;
                while (j < pattern.size() && (std::isalnum((unsigned char)pattern[j]) || pattern[j] == '_'))
                    ++j;
                names.emplace_back(pattern.substr(i + 1, j - (i + 1)));
                re_str += "([^/]+)"; // capture one segment
                i = j;
            }
            else
            {
                re_str += escape_regex_char(ch);
                ++i;
            }
        }
        re_str += "$";

        CompiledRoute cr{m, pattern, std::regex(re_str), names, std::move(h)};
        return cr;
    }

    struct Server::Impl
    {
        Options opts;

        // group prefix stack
        std::vector<std::string> group_stack;

        // middlewares
        std::vector<Middleware> pre_mw;
        std::vector<Middleware> post_mw;

        // routes
        std::vector<CompiledRoute> routes;

        // lifecycle
        std::atomic<bool> running{false};
        std::string listen_host = "0.0.0.0";
        uint16_t listen_port = 0;
        bool tls_enabled = false;

        // health check
        std::function<bool()> health_cb;

        // background jobs mgmt
        std::mutex jobs_mtx;
        std::vector<std::thread> bg_threads;

        Impl(const Options &o) : opts(o) {}
        ~Impl()
        {
            // stop background threads that use schedule_every loop by toggling running
            running = false;
            for (auto &t : bg_threads)
            {
                if (t.joinable())
                    t.detach(); // fire-and-forget style by design
            }
        }

        std::string current_prefix() const
        {
            if (group_stack.empty())
                return {};
            std::string p;
            for (auto &s : group_stack)
            {
                if (!s.empty() && s.back() == '/' && !p.empty() && p.back() == '/')
                    p.pop_back();
                p += s;
            }
            return p;
        }

        // Helper to execute middleware chain + handler
        void run_chain(Request &req, Response &res, const Handler &h)
        {
            // Build a single vector: pre_mw -> handler -> post_mw
            // Each middleware has signature (req, res, next).
            std::vector<std::function<void()>> steps;
            steps.reserve(pre_mw.size() + 1 + post_mw.size());

            size_t idx = 0;

            // Wrap pre middlewares
            for (auto &mw : pre_mw)
            {
                steps.emplace_back([&, i = idx++]()
                                   { mw(req, res, [&]()
                                        {
                    if (i + 1 < steps.size()) steps[i + 1](); }); });
            }

            // Handler itself
            steps.emplace_back([&, i = idx++]()
                               {
            h(req, res);
            if (i + 1 < steps.size()) steps[i + 1](); });

            // Post middlewares
            for (auto &mw : post_mw)
            {
                steps.emplace_back([&, i = idx++]()
                                   { mw(req, res, [&]()
                                        {
                    if (i + 1 < steps.size()) steps[i + 1](); }); });
            }

            if (!steps.empty())
                steps[0]();
        }

        bool dispatch(Request &req, Response &res)
        {
            // metrics endpoint
            if (opts.metrics_path && req.path == *opts.metrics_path)
            {
                // minimal text metrics (example)
                res.set_header("Content-Type", "text/plain; charset=utf-8");
                res.text("socketify_requests_total 1\n", 200);
                return true;
            }
            // health endpoint
            if (opts.health_path && req.path == *opts.health_path)
            {
                bool ok = health_cb ? health_cb() : true;
                res.text(ok ? "ok" : "unhealthy", ok ? 200 : 503);
                return true;
            }

            for (auto &r : routes)
            {
                if (r.method != HttpMethod::Any && r.method != req.method)
                    continue;
                std::smatch m;
                if (std::regex_match(req.path, m, r.re))
                {
                    // fill params
                    for (size_t i = 0; i < r.names.size(); ++i)
                    {
                        req.params[r.names[i]] = m[i + 1];
                    }
                    req.route_pattern = r.pattern;
                    run_chain(req, res, r.handler);
                    return true;
                }
            }
            return false;
        }
    };

    // --------------------------- Server public API --------------------------

    Server::Server(const Options &opts) : impl_(std::make_unique<Impl>(opts)) {}
    Server::~Server() = default;

    // Middleware
    Server &Server::use(Middleware mw)
    {
        impl_->post_mw.emplace_back(std::move(mw));
        return *this;
    }
    Server &Server::use_before_routing(Middleware mw)
    {
        impl_->pre_mw.emplace_back(std::move(mw));
        return *this;
    }
    Server &Server::use_after_routing(Middleware mw)
    {
        impl_->post_mw.emplace_back(std::move(mw));
        return *this;
    }

    // Routing (+ group prefixing)
    Server &Server::route(HttpMethod m, std::string_view pattern, Handler h)
    {
        std::string full = impl_->current_prefix();
        if (!full.empty())
        {
            // normalize slashes when concatenating
            if (full.back() == '/' && !pattern.empty() && pattern.front() == '/')
            {
                full.pop_back();
            }
            full += std::string(pattern);
        }
        else
        {
            full = std::string(pattern);
        }
        impl_->routes.emplace_back(compile_route(m, full, std::move(h)));
        return *this;
    }

    Server &Server::get(std::string_view p, Handler h) { return route(HttpMethod::Get, p, std::move(h)); }
    Server &Server::post(std::string_view p, Handler h) { return route(HttpMethod::Post, p, std::move(h)); }
    Server &Server::put(std::string_view p, Handler h) { return route(HttpMethod::Put, p, std::move(h)); }
    Server &Server::patch(std::string_view p, Handler h) { return route(HttpMethod::Patch, p, std::move(h)); }
    Server &Server::del(std::string_view p, Handler h) { return route(HttpMethod::Delete_, p, std::move(h)); }
    Server &Server::options(std::string_view p, Handler h) { return route(HttpMethod::Options, p, std::move(h)); }
    Server &Server::head(std::string_view p, Handler h) { return route(HttpMethod::Head, p, std::move(h)); }

    // Route groups / sub-routers
    Server &Server::group(std::string_view base, const std::function<void(Server &)> &builder)
    {
        // push prefix
        std::string b(base);
        if (b.empty() || b[0] != '/')
            b.insert(b.begin(), '/');
        impl_->group_stack.push_back(std::move(b));
        // let user add routes
        builder(*this);
        // pop prefix
        impl_->group_stack.pop_back();
        return *this;
    }

    // Static files (store options; actual serving should be done in your HTTP layer)
    Server &Server::static_dir(const StaticOptions &opt)
    {
        impl_->opts.static_files = opt;
        return *this;
    }

    // Convenience toggles
    Server &Server::enable_cors(const CorsOptions &opt)
    {
        impl_->opts.cors = opt;
        return *this;
    }
    Server &Server::enable_compression(const CompressionOptions &opt)
    {
        impl_->opts.compression = opt;
        return *this;
    }
    Server &Server::enable_rate_limit(const RateLimitOptions &opt)
    {
        impl_->opts.rate_limit = opt;
        return *this;
    }
    Server &Server::enable_sessions(const SessionOptions &opt)
    {
        impl_->opts.sessions = opt;
        return *this;
    }

    // TLS control
    Server &Server::enable_tls(const TlsConfig &tls)
    {
        impl_->opts.tls = tls;
        impl_->tls_enabled = (tls.mode != TlsMode::Disabled);
        return *this;
    }
    Server &Server::enable_tls_from_env(const std::string &prefix)
    {
        auto cfg = TlsConfig::from_env(prefix);
        return enable_tls(cfg);
    }

    // Observability
    Server &Server::with_metrics(std::string_view path)
    {
        impl_->opts.metrics_path = std::string(path);
        return *this;
    }
    Server &Server::with_healthcheck(std::string_view path, std::function<bool()> check)
    {
        impl_->opts.health_path = std::string(path);
        impl_->health_cb = std::move(check);
        return *this;
    }

    // Background tasks
    Server &Server::schedule_cron(std::string_view /*spec*/, Job /*job*/)
    {
        // Placeholder: wire a real cron parser later (eg. cronexpr).
        // For now we no-op to keep API compatible.
        return *this;
    }

    Server &Server::schedule_every(std::chrono::milliseconds period, Job job)
    {
        std::lock_guard<std::mutex> lk(impl_->jobs_mtx);
        impl_->bg_threads.emplace_back([this, period, job = std::move(job)]() mutable
                                       {
        while (impl_->running.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(period);
            try { job(); } catch (...) {
                // swallow to keep scheduler alive
            }
        } });
        impl_->bg_threads.back().detach();
        return *this;
    }

    // Lifecycle
    void Server::listen(std::string_view host, uint16_t port)
    {
        impl_->listen_host = std::string(host);
        impl_->listen_port = port;
        impl_->tls_enabled = false;
        // Your socket accept loop should be set up in run()
        std::cout << "[socketify] listening on " << impl_->listen_host << ":" << impl_->listen_port
                  << (impl_->tls_enabled ? " (TLS)" : "") << "\n";
    }

    void Server::listen_tls(std::string_view host, uint16_t port, const TlsConfig &tls)
    {
        enable_tls(tls);
        impl_->listen_host = std::string(host);
        impl_->listen_port = port;
        std::cout << "[socketify] listening on " << impl_->listen_host << ":" << impl_->listen_port
                  << " (TLS)\n";
    }

    static std::atomic<bool> g_stop_flag{false};
    static void sig_handler(int)
    {
        g_stop_flag = true;
    }

    void Server::run()
    {
        impl_->running = true;

        // Basic signal handling for graceful stop
        std::signal(SIGINT, sig_handler);
        std::signal(SIGTERM, sig_handler);

        std::cout << "[socketify] server running with "
                  << (impl_->opts.threads ? impl_->opts.threads : (int)std::thread::hardware_concurrency())
                  << " threads"
                  << (impl_->tls_enabled ? " (TLS enabled)" : "")
                  << "\n";

        // NOTE:
        // You should put your real accept loop here (epoll/kqueue/IOCP).
        // For now we idle until a signal is received.
        while (impl_->running && !g_stop_flag.load(std::memory_order_relaxed))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        shutdown_gracefully();
    }

    void Server::shutdown_gracefully()
    {
        if (!impl_->running.exchange(false))
            return;
        std::cout << "[socketify] shutting down gracefully...\n";
        // Close listeners, drain queues, flush logs, etc.
    }



} // namespace socketify
