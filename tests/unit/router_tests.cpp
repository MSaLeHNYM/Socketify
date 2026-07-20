// Unit tests for the Router: static/param/wildcard matching, groups,
// middleware order, 404/405 semantics.

#include "socketify/router.h"

#include <gtest/gtest.h>

using namespace socketify;

namespace {

Request make_req(Method m, std::string path) {
    Request r;
    r.set_method(m);
    r.set_path(std::move(path));
    return r;
}

} // namespace

TEST(Router, MatchesStaticRoute) {
    Router r;
    bool hit = false;
    r.AddRoute(Method::GET, "/hello", [&](Request&, Response& res) {
        hit = true;
        res.send("hi");
    });

    auto req = make_req(Method::GET, "/hello");
    Response res;
    EXPECT_TRUE(r.dispatch(req, res));
    EXPECT_TRUE(hit);
    EXPECT_EQ(res.body_view(), "hi");
}

TEST(Router, RootPattern) {
    Router r;
    r.AddRoute(Method::GET, "/", [](Request&, Response& res) { res.send("root"); });

    auto req = make_req(Method::GET, "/");
    Response res;
    EXPECT_TRUE(r.dispatch(req, res));
    EXPECT_EQ(res.body_view(), "root");
}

TEST(Router, NoMatchReturnsFalse) {
    Router r;
    r.AddRoute(Method::GET, "/a", [](Request&, Response& res) { res.send("a"); });

    auto req = make_req(Method::GET, "/nope");
    Response res;
    EXPECT_FALSE(r.dispatch(req, res));
    EXPECT_FALSE(res.ended());
}

TEST(Router, BindsPathParams) {
    Router r;
    std::string got_id, got_book;
    r.AddRoute(Method::GET, "/users/:id/books/:book", [&](Request& rq, Response& rs) {
        got_id = rq.params().at("id");
        got_book = rq.params().at("book");
        rs.send("ok");
    });

    auto req = make_req(Method::GET, "/users/42/books/dune");
    Response res;
    EXPECT_TRUE(r.dispatch(req, res));
    EXPECT_EQ(got_id, "42");
    EXPECT_EQ(got_book, "dune");
}

TEST(Router, WildcardCapturesRest) {
    Router r;
    std::string rest;
    r.AddRoute(Method::GET, "/files/*path", [&](Request& rq, Response& rs) {
        rest = rq.params().at("path");
        rs.send("ok");
    });

    auto req = make_req(Method::GET, "/files/a/b/c.txt");
    Response res;
    EXPECT_TRUE(r.dispatch(req, res));
    EXPECT_EQ(rest, "a/b/c.txt");
}

TEST(Router, WildcardMatchesEmpty) {
    Router r;
    std::string rest = "unset";
    r.AddRoute(Method::GET, "/files/*path", [&](Request& rq, Response& rs) {
        rest = rq.params().at("path");
        rs.send("ok");
    });

    auto req = make_req(Method::GET, "/files");
    Response res;
    EXPECT_TRUE(r.dispatch(req, res));
    EXPECT_EQ(rest, "");
}

TEST(Router, MethodNotAllowedSends405WithAllow) {
    Router r;
    r.AddRoute(Method::GET, "/thing", [](Request&, Response& rs) { rs.send("g"); });
    r.AddRoute(Method::POST, "/thing", [](Request&, Response& rs) { rs.send("p"); });

    auto req = make_req(Method::DELETE_, "/thing");
    Response res;
    EXPECT_TRUE(r.dispatch(req, res)); // handled (405 sent)
    EXPECT_EQ(res.status_code(), 405);
    auto allow = res.headers().at("Allow");
    EXPECT_NE(allow.find("GET"), std::string::npos);
    EXPECT_NE(allow.find("POST"), std::string::npos);
    EXPECT_NE(allow.find("HEAD"), std::string::npos); // implied by GET
}

TEST(Router, HeadFallsBackToGet) {
    Router r;
    r.AddRoute(Method::GET, "/page", [](Request&, Response& rs) { rs.send("body"); });

    auto req = make_req(Method::HEAD, "/page");
    Response res;
    EXPECT_TRUE(r.dispatch(req, res));
    EXPECT_EQ(res.status_code(), 200);
}

TEST(Router, AnyMatchesEveryMethod) {
    Router r;
    r.AddRoute(Method::ANY, "/any", [](Request&, Response& rs) { rs.send("x"); });

    for (Method m : {Method::GET, Method::POST, Method::PUT, Method::DELETE_}) {
        auto req = make_req(m, "/any");
        Response res;
        EXPECT_TRUE(r.dispatch(req, res)) << to_string(m);
    }
}

TEST(Router, GlobalMiddlewareRunsInOrder) {
    Router r;
    std::vector<int> order;
    r.Use([&](Request&, Response&, Next next) { order.push_back(1); next(); });
    r.Use([&](Request&, Response&, Next next) { order.push_back(2); next(); });
    r.AddRoute(Method::GET, "/x", [&](Request&, Response& rs) {
        order.push_back(3);
        rs.send("ok");
    });

    auto req = make_req(Method::GET, "/x");
    Response res;
    EXPECT_TRUE(r.dispatch(req, res));
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
}

TEST(Router, MiddlewareCanShortCircuit) {
    Router r;
    bool handler_ran = false;
    r.Use([](Request&, Response& rs, Next) {
        rs.status(Status::Unauthorized).send("no");
    });
    r.AddRoute(Method::GET, "/x", [&](Request&, Response& rs) {
        handler_ran = true;
        rs.send("ok");
    });

    auto req = make_req(Method::GET, "/x");
    Response res;
    EXPECT_TRUE(r.dispatch(req, res)); // ended by middleware
    EXPECT_FALSE(handler_ran);
    EXPECT_EQ(res.status_code(), 401);
}

TEST(Router, PerRouteMiddleware) {
    Router r;
    std::vector<int> order;
    r.AddRoute(Method::GET, "/x", [&](Request&, Response& rs) {
        order.push_back(2);
        rs.send("ok");
    }).Use([&](Request&, Response&, Next next) {
        order.push_back(1);
        next();
    });

    auto req = make_req(Method::GET, "/x");
    Response res;
    EXPECT_TRUE(r.dispatch(req, res));
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
}

TEST(Router, GroupPrefixAndMiddleware) {
    Router r;
    std::vector<std::string> order;
    auto& api = r.Group("/api");
    api.Use([&](Request&, Response&, Next next) {
        order.push_back("group-mw");
        next();
    });
    api.Get("/users", [&](Request&, Response& rs) {
        order.push_back("handler");
        rs.send("ok");
    });

    auto req = make_req(Method::GET, "/api/users");
    Response res;
    EXPECT_TRUE(r.dispatch(req, res));
    EXPECT_EQ(order, (std::vector<std::string>{"group-mw", "handler"}));
}

TEST(Router, GroupMiddlewareNotAppliedToSimilarPrefix) {
    Router r;
    bool group_mw_ran = false;
    auto& api = r.Group("/api");
    api.Use([&](Request&, Response&, Next next) {
        group_mw_ran = true;
        next();
    });
    // Not part of the group but shares the string prefix "/api".
    r.AddRoute(Method::GET, "/apiv2/users", [](Request&, Response& rs) { rs.send("ok"); });

    auto req = make_req(Method::GET, "/apiv2/users");
    Response res;
    EXPECT_TRUE(r.dispatch(req, res));
    EXPECT_FALSE(group_mw_ran);
}

TEST(Router, HandlerWithoutEndIsStillHandled) {
    Router r;
    r.AddRoute(Method::GET, "/lazy", [](Request&, Response& rs) {
        rs.status(Status::Accepted); // never calls send()/end()
    });

    auto req = make_req(Method::GET, "/lazy");
    Response res;
    // dispatch reports handled even though the response was not ended.
    EXPECT_TRUE(r.dispatch(req, res));
    EXPECT_FALSE(res.ended());
    EXPECT_EQ(res.status_code(), 202);
}

TEST(Router, PathsAreCaseSensitive) {
    Router r;
    r.AddRoute(Method::GET, "/CaseSensitive", [](Request&, Response& rs) { rs.send("ok"); });

    auto req = make_req(Method::GET, "/casesensitive");
    Response res;
    EXPECT_FALSE(r.dispatch(req, res));
}

TEST(Router, TrailingSlashIsIgnoredInSegments) {
    Router r;
    r.AddRoute(Method::GET, "/a/b", [](Request&, Response& rs) { rs.send("ok"); });

    auto req = make_req(Method::GET, "/a/b/");
    Response res;
    EXPECT_TRUE(r.dispatch(req, res));
}
