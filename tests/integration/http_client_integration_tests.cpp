#include "socketify/http_client.h"
#include "socketify/socketify.h"

#include <gtest/gtest.h>

#include <thread>

using namespace socketify;

class HttpClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        server_ = std::make_unique<Server>();
        server_->Get("/ping", [](Request&, Response& res) { res.json({{"ok", true}}); });
        server_->Post("/echo", [](Request& req, Response& res) {
            res.json({{"body", std::string(req.body_view())}});
        });
        ASSERT_TRUE(server_->Run("127.0.0.1", 0)) << server_->last_error();
        port_ = server_->port();
        base_ = "http://127.0.0.1:" + std::to_string(port_);
    }

    void TearDown() override { server_->Stop(); }

    std::unique_ptr<Server> server_;
    uint16_t port_{0};
    std::string base_;
};

TEST_F(HttpClientTest, GetJson) {
    auto res = http_client::get(base_ + "/ping");
    ASSERT_TRUE(res.ok()) << res.error;
    EXPECT_EQ(res.status, 200);
    auto j = res.json();
    ASSERT_TRUE(j.has_value());
    EXPECT_TRUE((*j)["ok"].get<bool>());
}

TEST_F(HttpClientTest, PostEcho) {
    auto res = http_client::post(base_ + "/echo", R"({"x":1})");
    ASSERT_TRUE(res.ok()) << res.error;
    auto j = res.json();
    ASSERT_TRUE(j.has_value());
    EXPECT_NE((*j)["body"].get<std::string>().find("x"), std::string::npos);
}

TEST_F(HttpClientTest, InvalidUrl) {
    auto res = http_client::get("not-a-url");
    EXPECT_EQ(res.status, 0);
    EXPECT_FALSE(res.error.empty());
}
