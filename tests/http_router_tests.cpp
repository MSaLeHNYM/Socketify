// This is a placeholder for router tests.
// A testing framework like GTest or Catch2 would be used here.

#include "socketify/router.h"
#include <cassert>
#include <iostream>

void test_static_routes() {
    socketify::Router router;
    bool called = false;

    router.AddRoute(socketify::Method::GET, "/hello", [&](auto&, auto&) {
        called = true;
    });

    socketify::Request req;
    req.set_method(socketify::Method::GET);
    req.set_path("/hello");

    socketify::Response res;
    router.dispatch(req, res);

    assert(called);
    std::cout << "Test static routes: PASSED\n";
}

void test_param_routes() {
    socketify::Router router;
    std::string user_id;

    router.AddRoute(socketify::Method::GET, "/users/:id", [&](auto& req, auto&) {
        user_id = req.params().at("id");
    });

    socketify::Request req;
    req.set_method(socketify::Method::GET);
    req.set_path("/users/123");

    socketify::Response res;
    router.dispatch(req, res);

    assert(user_id == "123");
    std::cout << "Test param routes: PASSED\n";
}

void test_wildcard_routes() {
    socketify::Router router;
    std::string path;

    router.AddRoute(socketify::Method::GET, "/static/*", [&](auto& req, auto&) {
        path = req.params().at("*");
    });

    socketify::Request req;
    req.set_method(socketify::Method::GET);
    req.set_path("/static/css/style.css");

    socketify::Response res;
    router.dispatch(req, res);

    assert(path == "css/style.css");
    std::cout << "Test wildcard routes: PASSED\n";
}

void test_middleware() {
    socketify::Router router;
    bool mw_called = false;
    bool route_called = false;

    router.Use([&](auto&, auto&, auto next) {
        mw_called = true;
        next();
    });

    router.AddRoute(socketify::Method::GET, "/", [&](auto&, auto&) {
        route_called = true;
    });

    socketify::Request req;
    req.set_method(socketify::Method::GET);
    req.set_path("/");

    socketify::Response res;
    router.dispatch(req, res);

    assert(mw_called);
    assert(route_called);
    std::cout << "Test middleware: PASSED\n";
}


int main() {
    test_static_routes();
    test_param_routes();
    test_wildcard_routes();
    test_middleware();

    std::cout << "\nAll router tests passed!\n";
    return 0;
}