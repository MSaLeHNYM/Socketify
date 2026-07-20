// Integration tests: real Server on an ephemeral port, exercised over
// raw sockets (keep-alive, pipelining, bodies, compression, files...).

#include "socketify/socketify.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "integration/test_client.h"

using namespace socketify;
using testclient::TcpClient;
using testclient::request;
using testclient::simple_get;

namespace fs = std::filesystem;

class ServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        ServerOptions opts;
        opts.workers = 2;
        opts.max_body_size = 64 * 1024;
        opts.compression.min_size = 32;
        server_ = std::make_unique<Server>(opts);

        server_->Get("/hello", [](Request&, Response& res) { res.send("world"); });

        server_->Get("/query", [](Request& req, Response& res) {
            res.json({{"id", std::string(req.query_value("id"))},
                      {"name", std::string(req.query_value("name"))}});
        });

        server_->Get("/users/:id", [](Request& req, Response& res) {
            res.send(req.params().at("id"));
        });

        server_->Post("/echo", [](Request& req, Response& res) {
            res.send(req.body_view(), std::string(req.content_type().empty()
                                                       ? "application/octet-stream"
                                                       : req.content_type()));
        });

        server_->Get("/big", [](Request&, Response& res) {
            res.send(std::string(8192, 'x'), "text/plain; charset=utf-8");
        });

        server_->Get("/cookie", [](Request&, Response& res) {
            res.set_cookie(Cookie("a", "1").path("/"));
            res.set_cookie(Cookie("b", "2").http_only());
            res.send("ok");
        });

        server_->Get("/lazy", [](Request&, Response& res) {
            res.status(Status::Accepted); // no send()/end()
        });

        server_->Get("/boom", [](Request&, Response&) {
            throw std::runtime_error("kaboom");
        });

        server_->Get("/redirect", [](Request&, Response& res) { res.redirect("/hello"); });

        ASSERT_TRUE(server_->Run("127.0.0.1", 0)) << server_->last_error();
        port_ = server_->port();
        ASSERT_NE(port_, 0);
    }

    void TearDown() override { server_->Stop(); }

    std::unique_ptr<Server> server_;
    uint16_t port_{0};
};

TEST_F(ServerTest, BasicGet) {
    auto r = request(port_, simple_get("/hello"));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->status, 200);
    EXPECT_EQ(r->body, "world");
    EXPECT_EQ(r->headers.at("content-length"), "5");
}

TEST_F(ServerTest, NotFound) {
    auto r = request(port_, simple_get("/missing"));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->status, 404);
}

TEST_F(ServerTest, MethodNotAllowed) {
    auto r = request(port_, "DELETE /hello HTTP/1.1\r\nHost: t\r\n\r\n");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->status, 405);
    EXPECT_NE(r->headers.at("allow").find("GET"), std::string::npos);
}

TEST_F(ServerTest, QueryStringParsing) {
    auto r = request(port_, simple_get("/query?id=42&name=John%20Doe"));
    ASSERT_TRUE(r.has_value());
    auto j = nlohmann::json::parse(r->body);
    EXPECT_EQ(j["id"], "42");
    EXPECT_EQ(j["name"], "John Doe");
}

TEST_F(ServerTest, PathParams) {
    auto r = request(port_, simple_get("/users/1234"));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->body, "1234");
}

TEST_F(ServerTest, PostBodyEcho) {
    std::string req =
        "POST /echo HTTP/1.1\r\nHost: t\r\nContent-Type: application/json\r\n"
        "Content-Length: 13\r\n\r\n{\"hi\":\"there\"}";
    // Content-Length 13 is wrong on purpose? No: {"hi":"there"} is 14. Use exact.
    std::string body = R"({"hi":"there"})";
    req = "POST /echo HTTP/1.1\r\nHost: t\r\nContent-Type: application/json\r\n"
          "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    auto r = request(port_, req);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->status, 200);
    EXPECT_EQ(r->body, body);
    EXPECT_NE(r->headers.at("content-type").find("application/json"), std::string::npos);
}

TEST_F(ServerTest, ChunkedRequestBody) {
    std::string req =
        "POST /echo HTTP/1.1\r\nHost: t\r\nTransfer-Encoding: chunked\r\n\r\n"
        "5\r\nhello\r\n"
        "1\r\n \r\n"
        "5\r\nworld\r\n"
        "0\r\n\r\n";
    auto r = request(port_, req);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->status, 200);
    EXPECT_EQ(r->body, "hello world");
}

TEST_F(ServerTest, KeepAliveServesMultipleRequests) {
    TcpClient c;
    ASSERT_TRUE(c.connect_to(port_));

    ASSERT_TRUE(c.send_all(simple_get("/hello")));
    auto r1 = c.read_response();
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(r1->body, "world");

    ASSERT_TRUE(c.send_all(simple_get("/users/7")));
    auto r2 = c.read_response();
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r2->body, "7");
}

TEST_F(ServerTest, PipelinedRequests) {
    TcpClient c;
    ASSERT_TRUE(c.connect_to(port_));
    // Two requests in one write.
    ASSERT_TRUE(c.send_all(simple_get("/hello") + simple_get("/users/9")));
    auto r1 = c.read_response();
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(r1->body, "world");
    auto r2 = c.read_response();
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r2->body, "9");
}

TEST_F(ServerTest, ConnectionCloseHonored) {
    TcpClient c;
    ASSERT_TRUE(c.connect_to(port_));
    ASSERT_TRUE(c.send_all(simple_get("/hello", "Connection: close\r\n")));
    auto r = c.read_response();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->headers.at("connection"), "close");
    // Server should close: next read returns EOF quickly.
    std::string rest;
    c.read_until(rest, [](const std::string&) { return false; }, 500);
    EXPECT_TRUE(rest.empty());
}

TEST_F(ServerTest, Http10DefaultsToClose) {
    auto r = request(port_, "GET /hello HTTP/1.0\r\nHost: t\r\n\r\n");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->headers.at("connection"), "close");
}

TEST_F(ServerTest, GzipCompression) {
    auto r = request(port_, simple_get("/big", "Accept-Encoding: gzip\r\n"));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->headers.at("content-encoding"), "gzip");
    EXPECT_LT(std::atoll(r->headers.at("content-length").c_str()), 8192);
    // gzip magic
    ASSERT_GE(r->body.size(), 2u);
    EXPECT_EQ(static_cast<unsigned char>(r->body[0]), 0x1f);
    EXPECT_EQ(static_cast<unsigned char>(r->body[1]), 0x8b);
}

TEST_F(ServerTest, NoCompressionWithoutAcceptEncoding) {
    auto r = request(port_, simple_get("/big"));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->headers.count("content-encoding"), 0u);
    EXPECT_EQ(r->body.size(), 8192u);
}

TEST_F(ServerTest, HeadStripsBody) {
    auto r = request(port_, "HEAD /hello HTTP/1.1\r\nHost: t\r\n\r\n");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->status, 200);
    EXPECT_EQ(r->headers.at("content-length"), "5");
    EXPECT_TRUE(r->body.empty());
}

TEST_F(ServerTest, MultipleSetCookieLines) {
    TcpClient c;
    ASSERT_TRUE(c.connect_to(port_));
    ASSERT_TRUE(c.send_all(simple_get("/cookie")));
    std::string raw;
    c.read_until(raw, [](const std::string& b) {
        return b.find("\r\n\r\nok") != std::string::npos;
    });
    // Two distinct Set-Cookie lines, not comma-joined.
    EXPECT_NE(raw.find("Set-Cookie: a=1; Path=/\r\n"), std::string::npos);
    EXPECT_NE(raw.find("Set-Cookie: b=2; HttpOnly\r\n"), std::string::npos);
}

TEST_F(ServerTest, LazyHandlerAutoFinalized) {
    auto r = request(port_, simple_get("/lazy"));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->status, 202); // not 404
}

TEST_F(ServerTest, HandlerExceptionYields500) {
    auto r = request(port_, simple_get("/boom"));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->status, 500);
    // The connection survives (keep-alive) and the server still works.
    auto r2 = request(port_, simple_get("/hello"));
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r2->status, 200);
}

TEST_F(ServerTest, Redirect) {
    auto r = request(port_, simple_get("/redirect"));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->status, 302);
    EXPECT_EQ(r->headers.at("location"), "/hello");
}

TEST_F(ServerTest, OversizedBodyGets413) {
    std::string req =
        "POST /echo HTTP/1.1\r\nHost: t\r\nContent-Length: 999999999\r\n\r\n";
    auto r = request(port_, req);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->status, 413);
}

TEST_F(ServerTest, MalformedRequestGets400) {
    auto r = request(port_, "NONSENSE\r\n\r\n");
    ASSERT_TRUE(r.has_value());
    EXPECT_GE(r->status, 400);
}

TEST_F(ServerTest, UnsupportedVersionGets505) {
    auto r = request(port_, "GET / HTTP/3.0\r\nHost: t\r\n\r\n");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->status, 505);
}

TEST_F(ServerTest, PercentEncodedPath) {
    // /users/:id with an encoded id
    auto r = request(port_, simple_get("/users/a%20b"));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->body, "a b");
}

TEST_F(ServerTest, Expect100Continue) {
    TcpClient c;
    ASSERT_TRUE(c.connect_to(port_));
    std::string head =
        "POST /echo HTTP/1.1\r\nHost: t\r\nContent-Length: 5\r\n"
        "Expect: 100-continue\r\n\r\n";
    ASSERT_TRUE(c.send_all(head));
    std::string interim;
    ASSERT_TRUE(c.read_until(interim, [](const std::string& b) {
        return b.find("100 Continue") != std::string::npos;
    }));
    ASSERT_TRUE(c.send_all("hello"));
    auto r = c.read_response();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->status, 200);
    EXPECT_EQ(r->body, "hello");
}

// ---------------------------------------------------------------------------
// Static file streaming through a live server
// ---------------------------------------------------------------------------

class StaticServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / ("socketify_static_" + std::to_string(::getpid()));
        fs::create_directories(root_);
        content_.assign(100000, 'z');
        for (std::size_t i = 0; i < content_.size(); i += 997) content_[i] = char('a' + (i % 26));
        std::ofstream(root_ / "large.bin", std::ios::binary).write(content_.data(),
                                                                   static_cast<std::streamsize>(content_.size()));

        server_ = std::make_unique<Server>();
        server_->Use(static_files::serve(root_.string()));
        ASSERT_TRUE(server_->Run("127.0.0.1", 0));
        port_ = server_->port();
    }

    void TearDown() override {
        server_->Stop();
        std::error_code ec;
        fs::remove_all(root_, ec);
    }

    fs::path root_;
    std::string content_;
    std::unique_ptr<Server> server_;
    uint16_t port_{0};
};

TEST_F(StaticServerTest, StreamsLargeFileViaSendfile) {
    auto r = request(port_, simple_get("/large.bin"), 5000);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->status, 200);
    ASSERT_EQ(r->body.size(), content_.size());
    EXPECT_EQ(r->body, content_);
}

TEST_F(StaticServerTest, RangeRequestOverWire) {
    auto r = request(port_, simple_get("/large.bin", "Range: bytes=100-199\r\n"));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->status, 206);
    EXPECT_EQ(r->body, content_.substr(100, 100));
}

TEST_F(StaticServerTest, HeadOnFile) {
    auto r = request(port_, "HEAD /large.bin HTTP/1.1\r\nHost: t\r\n\r\n");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->status, 200);
    EXPECT_EQ(r->headers.at("content-length"), std::to_string(content_.size()));
    EXPECT_TRUE(r->body.empty());
}
