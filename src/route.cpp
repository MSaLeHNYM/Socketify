#include "route.h"

namespace socketify {

void Router::get(const std::string& path, RouteHandler handler) {
    get_routes[path] = handler;
}

void Router::post(const std::string& path, RouteHandler handler) {
    post_routes[path] = handler;
}

RouteHandler Router::match(HttpMethod method, const std::string& path) {
    if (method == HttpMethod::GET && get_routes.count(path)) {
        return get_routes[path];
    }
    if (method == HttpMethod::POST && post_routes.count(path)) {
        return post_routes[path];
    }
    return nullptr;
}

} // namespace socketify