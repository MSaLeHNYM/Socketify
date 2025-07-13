#pragma once

#include "request.h"
#include "response.h"

namespace socketify {

using RouteHandler = std::function<void(const Request&, Response&)>;

class Router {
    std::unordered_map<std::string, RouteHandler> get_routes;
    std::unordered_map<std::string, RouteHandler> post_routes;

public:
    void get(const std::string& path, RouteHandler handler);
    void post(const std::string& path, RouteHandler handler);
    RouteHandler match(HttpMethod method, const std::string& path);
};

}
