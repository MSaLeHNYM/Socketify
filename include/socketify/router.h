#pragma once
// socketify/router.h — URL routing, middleware, and groups

#include "socketify/http.h"
#include "socketify/request.h"
#include "socketify/response.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace socketify {

// ---------- Handler & Middleware ----------
using Handler   = std::function<void(Request&, Response&)>;
using Next      = std::function<void()>;
using Middleware= std::function<void(Request&, Response&, Next)>;

// ---------- Route ----------
class Route {
public:
    Route(Method m, std::string pattern, Handler h)
        : method_(m), pattern_(std::move(pattern)), handler_(std::move(h)) {}

    Route& Use(Middleware mw) { middlewares_.push_back(std::move(mw)); return *this; }

    Method method() const noexcept { return method_; }
    std::string_view pattern() const noexcept { return pattern_; }
    const Handler& handler() const noexcept { return handler_; }
    const std::vector<Middleware>& middlewares() const noexcept { return middlewares_; }

private:
    Method method_;
    std::string pattern_;
    Handler handler_;
    std::vector<Middleware> middlewares_;

    friend class Router;
    struct Seg {
        enum Kind { Static, Param, Wildcard } kind;
        std::string text; // literal for Static, name for Param/Wildcard
    };
    std::vector<Seg> segs_;
};

// ---------- Router ----------
class Router {
public:
    Router() = default;

    // Inline for link-safety
    Route& AddRoute(Method m, std::string_view pattern, Handler h) {
        routes_.emplace_back(m, std::string(pattern), std::move(h));
        routes_.back().segs_ = compile_pattern_(routes_.back().pattern_);
        return routes_.back();
    }

    // Global middleware (applies to ALL requests, even when no route matches)
    Router& Use(Middleware mw) { global_mw_.push_back(std::move(mw)); return *this; }

    // Dispatch: run global middleware; then route matching; then per-route middleware+handler
    // Returns true if a route matched (or a middleware ended the response), false if 404.
    bool dispatch(Request& req, Response& res) const;

    // Group helper (prefix all patterns)
    class RouteGroup {
    public:
        RouteGroup(std::string prefix, Router& r) : prefix_(std::move(prefix)), router_(r) {}
        Route& AddRoute(Method m, std::string_view pattern, Handler h) {
            std::string full = prefix_;
            if (!full.empty() && full.back() == '/' && !pattern.empty() && pattern.front() == '/')
                full.pop_back();
            full.append(pattern);
            return router_.AddRoute(m, full, std::move(h));
        }
        RouteGroup& Use(Middleware mw) { group_mw_.push_back(std::move(mw)); return *this; }

        const std::string& prefix() const { return prefix_; }
        const std::vector<Middleware>& middlewares() const { return group_mw_; }
    private:
        std::string prefix_;
        Router& router_;
        std::vector<Middleware> group_mw_;
        friend class Router;
    };

    RouteGroup Group(std::string_view prefix) {
        groups_.emplace_back(std::string(prefix), *this);
        return groups_.back();
    }

private:
    std::vector<Route> routes_;
    std::vector<Middleware> global_mw_;
    mutable std::vector<RouteGroup> groups_;

    static std::vector<Route::Seg> compile_pattern_(std::string_view pattern);
    static bool match_and_bind_(std::string_view path,
                                const std::vector<Route::Seg>& segs,
                                Request& req);
    static std::vector<std::string_view> split_path_(std::string_view s);
    static bool starts_with_(std::string_view s, std::string_view pfx);
};

} // namespace socketify
