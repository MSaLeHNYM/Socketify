#pragma once
/**
 * @file router.h
 * @brief URL routing: pattern matching, middleware chains and route groups.
 *
 * Patterns support three segment kinds:
 *  - static:   "/users/list"
 *  - params:   "/users/:id" (bound into Request::params())
 *  - wildcard: "/files/" + "*path" (captures the remaining path)
 */

#include "socketify/http.h"
#include "socketify/middleware.h"
#include "socketify/request.h"
#include "socketify/response.h"

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace socketify {

/**
 * @brief A single registered route: method + pattern + handler.
 *
 * Returned by AddRoute so per-route middleware can be chained:
 * @code
 * server.AddRoute(Method::GET, "/admin", handler).Use(require_auth());
 * @endcode
 */
class Route {
public:
    /** @brief Construct a route (normally done through Router::AddRoute). */
    Route(Method m, std::string pattern, Handler h)
        : method_(m), pattern_(std::move(pattern)), handler_(std::move(h)) {}

    /** @brief Attach middleware that runs only for this route. */
    Route& Use(Middleware mw) { middlewares_.push_back(std::move(mw)); return *this; }

    /** @brief Method this route responds to. */
    Method method() const noexcept { return method_; }
    /** @brief Original pattern string. */
    std::string_view pattern() const noexcept { return pattern_; }
    /** @brief Terminal handler. */
    const Handler& handler() const noexcept { return handler_; }
    /** @brief Per-route middleware, in registration order. */
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

/**
 * @brief Routing table with global middleware and prefix groups.
 *
 * Dispatch order: global middleware (registration order) -> group middleware
 * of the matched route -> per-route middleware -> handler.
 */
class Router {
public:
    Router() = default;

    /**
     * @brief Register a route.
     * @param m       Method to match (Method::ANY matches all).
     * @param pattern Path pattern, e.g. "/users/:id" or a wildcard "*rest".
     * @param h       Terminal handler.
     * @return Reference to the created Route (for chaining .Use()).
     */
    Route& AddRoute(Method m, std::string_view pattern, Handler h) {
        routes_.emplace_back(m, std::string(pattern), std::move(h));
        routes_.back().segs_ = compile_pattern_(routes_.back().pattern_);
        return routes_.back();
    }

    /** @brief Register global middleware (runs for every request). */
    Router& Use(Middleware mw) { global_mw_.push_back(std::move(mw)); return *this; }

    /**
     * @brief Run middleware and route the request.
     *
     * Runs global middleware, matches a route, then runs group/route
     * middleware and the handler. Sends 405 with an Allow header when the
     * path matches but the method does not.
     *
     * @return true when the request was handled (a route matched or a
     *         middleware ended the response); false means "no route" and
     *         the caller should produce a 404.
     */
    bool dispatch(Request& req, Response& res) const;

    /**
     * @brief Route group: shares a path prefix and its own middleware.
     *
     * @code
     * auto api = server.Group("/api");
     * api.Use(rate_limiter);
     * api.Get("/users", list_users);
     * @endcode
     */
    class RouteGroup {
    public:
        /** @brief Construct through Router::Group(). */
        RouteGroup(std::string prefix, Router& r) : prefix_(std::move(prefix)), router_(r) {}

        /** @brief Register a route under this group's prefix. */
        Route& AddRoute(Method m, std::string_view pattern, Handler h) {
            std::string full = prefix_;
            if (!full.empty() && full.back() == '/' && !pattern.empty() && pattern.front() == '/')
                full.pop_back();
            full.append(pattern);
            return router_.AddRoute(m, full, std::move(h));
        }

        /** @name Express-style shorthands
         *  @{ */
        Route& Get(std::string_view p, Handler h)    { return AddRoute(Method::GET, p, std::move(h)); }
        Route& Post(std::string_view p, Handler h)   { return AddRoute(Method::POST, p, std::move(h)); }
        Route& Put(std::string_view p, Handler h)    { return AddRoute(Method::PUT, p, std::move(h)); }
        Route& Patch(std::string_view p, Handler h)  { return AddRoute(Method::PATCH, p, std::move(h)); }
        Route& Delete(std::string_view p, Handler h) { return AddRoute(Method::DELETE_, p, std::move(h)); }
        Route& Options(std::string_view p, Handler h){ return AddRoute(Method::OPTIONS, p, std::move(h)); }
        Route& Head(std::string_view p, Handler h)   { return AddRoute(Method::HEAD, p, std::move(h)); }
        Route& Any(std::string_view p, Handler h)    { return AddRoute(Method::ANY, p, std::move(h)); }
        /** @} */

        /** @brief Middleware that runs for every route in this group. */
        RouteGroup& Use(Middleware mw) { group_mw_.push_back(std::move(mw)); return *this; }

        /** @brief The group's path prefix. */
        const std::string& prefix() const { return prefix_; }
        /** @brief The group's middleware list. */
        const std::vector<Middleware>& middlewares() const { return group_mw_; }
    private:
        std::string prefix_;
        Router& router_;
        std::vector<Middleware> group_mw_;
        friend class Router;
    };

    /**
     * @brief Create a route group under @p prefix.
     * @return A stable reference owned by the router (safe to keep).
     */
    RouteGroup& Group(std::string_view prefix) {
        groups_.emplace_back(std::string(prefix), *this);
        return groups_.back();
    }

    /** @brief Internal helper exposed for prefix checks. */
    static bool starts_with_public_(std::string_view s, std::string_view pfx);

private:
    std::deque<Route> routes_;
    std::vector<Middleware> global_mw_;
    std::deque<RouteGroup> groups_;

    static std::vector<Route::Seg> compile_pattern_(std::string_view pattern);
    static bool match_and_bind_(std::string_view path,
                                const std::vector<Route::Seg>& segs,
                                ParamMap& params);
    static std::vector<std::string_view> split_path_(std::string_view s);
    static bool starts_with_(std::string_view s, std::string_view pfx);
};

} // namespace socketify
