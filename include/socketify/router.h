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
/**
 * @brief A function that handles a request.
 * @param req The request object.
 * @param res The response object.
 */
using Handler   = std::function<void(Request&, Response&)>;
/**
 * @brief A function that calls the next middleware in the chain.
 */
using Next      = std::function<void()>;
/**
 * @brief A function that processes a request before it reaches the handler.
 * @param req The request object.
 * @param res The response object.
 * @param next A function to call the next middleware.
 */
using Middleware= std::function<void(Request&, Response&, Next)>;

// ---------- Route ----------
/**
 * @brief Represents a route in the application.
 */
class Route {
public:
    /**
     * @brief Constructs a new Route.
     * @param m The HTTP method.
     * @param pattern The URL pattern.
     * @param h The handler function.
     */
    Route(Method m, std::string pattern, Handler h)
        : method_(m), pattern_(std::move(pattern)), handler_(std::move(h)) {}

    /**
     * @brief Adds a middleware to the route.
     * @param mw The middleware to add.
     * @return A reference to the Route.
     */
    Route& Use(Middleware mw) { middlewares_.push_back(std::move(mw)); return *this; }

    /**
     * @brief Gets the HTTP method of the route.
     * @return The HTTP method.
     */
    Method method() const noexcept { return method_; }
    /**
     * @brief Gets the URL pattern of the route.
     * @return The URL pattern.
     */
    std::string_view pattern() const noexcept { return pattern_; }
    /**
     * @brief Gets the handler function of the route.
     * @return The handler function.
     */
    const Handler& handler() const noexcept { return handler_; }
    /**
     * @brief Gets the middlewares of the route.
     * @return A const reference to the vector of middlewares.
     */
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
/**
 * @brief Manages routing of incoming requests to the appropriate handlers.
 */
class Router {
public:
    Router() = default;

    /**
     * @brief Adds a new route.
     * @param m The HTTP method.
     * @param pattern The URL pattern.
     * @param h The handler function.
     * @return A reference to the newly created Route.
     */
    Route& AddRoute(Method m, std::string_view pattern, Handler h) {
        routes_.emplace_back(m, std::string(pattern), std::move(h));
        routes_.back().segs_ = compile_pattern_(routes_.back().pattern_);
        return routes_.back();
    }

    /**
     * @brief Adds a global middleware.
     * @param mw The middleware to add.
     * @return A reference to the Router.
     */
    Router& Use(Middleware mw) { global_mw_.push_back(std::move(mw)); return *this; }

    /**
     * @brief Dispatches a request to the appropriate handler.
     * @param req The request object.
     * @param res The response object.
     * @return true if a route was matched, false otherwise.
     */
    bool dispatch(Request& req, Response& res) const;

    /**
     * @brief A helper class for creating route groups.
     */
    class RouteGroup {
    public:
        /**
         * @brief Constructs a new RouteGroup.
         * @param prefix The common prefix for all routes in the group.
         * @param r The router instance.
         */
        RouteGroup(std::string prefix, Router& r) : prefix_(std::move(prefix)), router_(r) {}
        /**
         * @brief Adds a new route to the group.
         * @param m The HTTP method.
         * @param pattern The URL pattern.
         * @param h The handler function.
         * @return A reference to the newly created Route.
         */
        Route& AddRoute(Method m, std::string_view pattern, Handler h) {
            std::string full = prefix_;
            if (!full.empty() && full.back() == '/' && !pattern.empty() && pattern.front() == '/')
                full.pop_back();
            full.append(pattern);
            return router_.AddRoute(m, full, std::move(h));
        }
        /**
         * @brief Adds a middleware to the group.
         * @param mw The middleware to add.
         * @return A reference to the RouteGroup.
         */
        RouteGroup& Use(Middleware mw) { group_mw_.push_back(std::move(mw)); return *this; }

        /**
         * @brief Gets the prefix of the route group.
         * @return The prefix.
         */
        const std::string& prefix() const { return prefix_; }
        /**
         * @brief Gets the middlewares of the route group.
         * @return A const reference to the vector of middlewares.
         */
        const std::vector<Middleware>& middlewares() const { return group_mw_; }
    private:
        std::string prefix_;
        Router& router_;
        std::vector<Middleware> group_mw_;
        friend class Router;
    };

    /**
     * @brief Creates a new route group.
     * @param prefix The common prefix for all routes in the group.
     * @return A RouteGroup object.
     */
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