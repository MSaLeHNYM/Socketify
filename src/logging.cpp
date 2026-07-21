/**
 * @file logging.cpp
 * @brief Logger core and request-logging middleware.
 */

#include "socketify/logging.h"
#include "socketify/detail/utils.h"
#include "socketify/http.h"
#include "socketify/request.h"
#include "socketify/response.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

namespace socketify::logging {

namespace {

std::atomic<Level> g_level{Level::Info};

std::mutex g_sink_mu;
Sink g_sink; // empty -> default stderr sink

const char* level_tag_(Level l) {
    switch (l) {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO ";
        case Level::Warn:  return "WARN ";
        case Level::Error: return "ERROR";
        case Level::Fatal: return "FATAL";
        default:           return "?????";
    }
}

void default_sink_(Level lvl, std::string_view msg) {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto secs = duration_cast<seconds>(now.time_since_epoch()).count();
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;

    std::tm tm{};
    time_t t = static_cast<time_t>(secs);
    localtime_r(&t, &tm);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm);

    std::fprintf(stderr, "%s.%03d [%s] %.*s\n", stamp, static_cast<int>(ms),
                 level_tag_(lvl), static_cast<int>(msg.size()), msg.data());
}

std::string client_ip_(const Request& req, bool trust_proxy) {
    if (trust_proxy) {
        auto xff = req.header("X-Forwarded-For");
        if (!xff.empty()) {
            auto comma = xff.find(',');
            auto hop = (comma == std::string_view::npos)
                           ? xff
                           : xff.substr(0, comma);
            while (!hop.empty() && (hop.front() == ' ' || hop.front() == '\t'))
                hop.remove_prefix(1);
            while (!hop.empty() && (hop.back() == ' ' || hop.back() == '\t'))
                hop.remove_suffix(1);
            if (!hop.empty()) return std::string(hop);
        }
    }
    return req.remote_ip().empty() ? "-" : std::string(req.remote_ip());
}

Level status_level_(unsigned status) {
    if (status >= 500) return Level::Error;
    if (status >= 400) return Level::Warn;
    return Level::Info;
}

void append_debug_extras_(std::string& line, const Request& req) {
    if (level() > Level::Debug) return;
    auto rid = req.header(H_XRequestId);
    if (!rid.empty()) {
        line.append(" rid=").append(rid);
    }
    auto ua = req.header("User-Agent");
    if (!ua.empty()) {
        constexpr std::size_t kMax = 40;
        line.append(" ua=");
        if (ua.size() <= kMax) {
            line.append(ua);
        } else {
            line.append(ua.substr(0, kMax)).append("...");
        }
    }
}

} // namespace

void set_level(Level lvl) { g_level.store(lvl, std::memory_order_relaxed); }
Level level() { return g_level.load(std::memory_order_relaxed); }

void set_sink(Sink sink) {
    std::lock_guard<std::mutex> lk(g_sink_mu);
    g_sink = std::move(sink);
}

void log(Level lvl, std::string_view message) {
    if (lvl < level() || lvl == Level::Off) return;
    Sink local;
    {
        std::lock_guard<std::mutex> lk(g_sink_mu);
        local = g_sink;
    }
    if (local) local(lvl, message);
    else default_sink_(lvl, message);
}

Middleware middleware(Options opts) {
    const bool common = (opts.format == "common");
    const bool trust_proxy = opts.trust_proxy;

    return [common, trust_proxy](Request& req, Response& res, Next next) {
        auto start = std::chrono::steady_clock::now();
        next();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                      std::chrono::steady_clock::now() - start).count();

        unsigned status = res.status_code() ? res.status_code() : 200u;
        std::size_t bytes = res.body_view().size();
        if (res.kind() == Response::Kind::File) {
            bytes = static_cast<std::size_t>(res.file_length());
        }

        const Level lvl = status_level_(status);
        const std::string ip = client_ip_(req, trust_proxy);

        std::string line;
        line.reserve(128);
        if (common) {
            // 127.0.0.1 - - [date] "GET /path HTTP/1.1" 200 123
            line.append(ip);
            line.append(" - - [").append(detail::http_date_now()).append("] \"");
            line.append(to_string(req.method())).append(" ");
            line.append(req.raw_target().empty() ? req.path() : req.raw_target()).append(" ");
            line.append(req.http_version().empty() ? "HTTP/1.1" : req.http_version()).append("\" ");
            line.append(std::to_string(status)).append(" ");
            line.append(std::to_string(bytes));
        } else {
            // 127.0.0.1 GET /path 200 1.234ms 56B
            line.append(ip).append(" ");
            line.append(to_string(req.method())).append(" ");
            line.append(req.path()).append(" ");
            line.append(std::to_string(status)).append(" ");
            line.append(std::to_string(us / 1000)).append(".");
            line.append(std::to_string((us % 1000) / 100)).append("ms ");
            line.append(std::to_string(bytes)).append("B");
        }
        append_debug_extras_(line, req);
        log(lvl, line);
    };
}

} // namespace socketify::logging
