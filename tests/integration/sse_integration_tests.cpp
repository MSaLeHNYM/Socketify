// Integration tests for Server-Sent Events over a live connection.

#include "socketify/socketify.h"

#include <gtest/gtest.h>

#include <mutex>

#include "integration/test_client.h"

using namespace socketify;
using testclient::TcpClient;
using testclient::simple_get;

class SseTest : public ::testing::Test {
protected:
    void SetUp() override {
        server_ = std::make_unique<Server>();
        server_->Get("/events", [this](Request& req, Response& res) {
            auto s = sse::upgrade(req, res);
            {
                std::lock_guard<std::mutex> lk(mu_);
                session_ = s;
            }
            s.send_event("welcome", "hello");
        });
        ASSERT_TRUE(server_->Run("127.0.0.1", 0));
        port_ = server_->port();
    }

    void TearDown() override { server_->Stop(); }

    sse::Session session() {
        std::lock_guard<std::mutex> lk(mu_);
        return session_;
    }

    std::unique_ptr<Server> server_;
    uint16_t port_{0};
    std::mutex mu_;
    sse::Session session_;
};

TEST_F(SseTest, StreamsEvents) {
    TcpClient c;
    ASSERT_TRUE(c.connect_to(port_));
    ASSERT_TRUE(c.send_all(simple_get("/events")));

    std::string buf;
    // Headers + the welcome event arrive first.
    ASSERT_TRUE(c.read_until(buf, [](const std::string& b) {
        return b.find("event: welcome") != std::string::npos &&
               b.find("data: hello") != std::string::npos;
    }));
    EXPECT_NE(buf.find("200 OK"), std::string::npos);
    EXPECT_NE(buf.find("Content-Type: text/event-stream"), std::string::npos);

    // Push more events from "another thread" (test thread).
    auto s = session();
    ASSERT_TRUE(s.valid());
    EXPECT_TRUE(s.alive());
    ASSERT_TRUE(s.send("plain message"));
    ASSERT_TRUE(s.send_event("tick", "42", "7"));

    ASSERT_TRUE(c.read_until(buf, [](const std::string& b) {
        return b.find("data: plain message") != std::string::npos &&
               b.find("id: 7") != std::string::npos &&
               b.find("event: tick") != std::string::npos &&
               b.find("data: 42") != std::string::npos;
    }));
}

TEST_F(SseTest, MultilineDataSplitsIntoDataLines) {
    TcpClient c;
    ASSERT_TRUE(c.connect_to(port_));
    ASSERT_TRUE(c.send_all(simple_get("/events")));
    std::string buf;
    ASSERT_TRUE(c.read_until(buf, [](const std::string& b) {
        return b.find("data: hello") != std::string::npos;
    }));

    auto s = session();
    ASSERT_TRUE(s.send("line1\nline2"));
    ASSERT_TRUE(c.read_until(buf, [](const std::string& b) {
        return b.find("data: line1\ndata: line2") != std::string::npos;
    }));
}

TEST_F(SseTest, CloseEndsConnection) {
    TcpClient c;
    ASSERT_TRUE(c.connect_to(port_));
    ASSERT_TRUE(c.send_all(simple_get("/events")));
    std::string buf;
    ASSERT_TRUE(c.read_until(buf, [](const std::string& b) {
        return b.find("data: hello") != std::string::npos;
    }));

    auto s = session();
    s.close();

    // Connection closes; further reads hit EOF (read_until returns with no
    // growth once the socket closes).
    std::string rest;
    c.read_until(rest, [](const std::string&) { return false; }, 1000);
    EXPECT_FALSE(s.alive());
    EXPECT_FALSE(s.send("after close"));
}

TEST_F(SseTest, SendAfterClientDisconnectFails) {
    {
        TcpClient c;
        ASSERT_TRUE(c.connect_to(port_));
        ASSERT_TRUE(c.send_all(simple_get("/events")));
        std::string buf;
        ASSERT_TRUE(c.read_until(buf, [](const std::string& b) {
            return b.find("data: hello") != std::string::npos;
        }));
        c.close(); // client goes away
    }

    auto s = session();
    ASSERT_TRUE(s.valid());
    // The server notices the disconnect; sends start failing (possibly after
    // the first send that triggers the write error).
    bool alive_after = true;
    for (int i = 0; i < 50 && alive_after; ++i) {
        s.send("probe");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        alive_after = s.alive();
    }
    EXPECT_FALSE(alive_after);
}
