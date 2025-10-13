#include <gtest/gtest.h>
#include <socketify/router.h>
#include <socketify/request.h>
#include <socketify/response.h>

using namespace socketify;

TEST(HttpRouter, BasicRouting) {
    Router router;
    bool called = false;
    router.AddRoute(Method::GET, "/", [&](Request&, Response&) {
        called = true;
    });

    Request req;
    req.set_method(Method::GET);
    req.set_path("/");
    Response res;

    router.dispatch(req, res);

    EXPECT_TRUE(called);
}

TEST(HttpRouter, ParamExtraction) {
    Router router;
    std::string name;
    router.AddRoute(Method::GET, "/hello/:name", [&](Request& req, Response&) {
        name = req.params().at("name");
    });

    Request req;
    req.set_method(Method::GET);
    req.set_path("/hello/world");
    Response res;

    router.dispatch(req, res);

    EXPECT_EQ(name, "world");
}

TEST(HttpRouter, NotFound) {
    Router router;
    bool called = false;
    router.AddRoute(Method::GET, "/", [&](Request&, Response&) {
        called = true;
    });

    Request req;
    req.set_method(Method::GET);
    req.set_path("/notfound");
    Response res;

    router.dispatch(req, res);

    EXPECT_FALSE(called);
    EXPECT_FALSE(res.ended());
}