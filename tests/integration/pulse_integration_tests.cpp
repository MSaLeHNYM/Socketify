// Integration tests for Pulse (WebSocket) over a live Server.

#include "socketify/socketify.h"

#include <gtest/gtest.h>

#include <mutex>
#include <string>

#include "integration/test_client.h"

using namespace socketify;
using testclient::TcpClient;

namespace {

std::string ws_handshake_(std::string_view path, std::string_view key = "dGhlIHNhbXBsZSBub25jZQ==") {
    std::string req;
    req += "GET ";
    req += path;
    req += " HTTP/1.1\r\n";
    req += "Host: 127.0.0.1\r\n";
    req += "Upgrade: websocket\r\n";
    req += "Connection: Upgrade\r\n";
    req += "Sec-WebSocket-Key: ";
    req += key;
    req += "\r\n";
    req += "Sec-WebSocket-Version: 13\r\n\r\n";
    return req;
}

std::string mask_text_frame_(std::string_view text) {
    std::string out;
    out.push_back(static_cast<char>(0x81));
    unsigned char mask[4] = {1, 2, 3, 4};
    if (text.size() < 126) {
        out.push_back(static_cast<char>(0x80 | text.size()));
    } else {
        out.push_back(static_cast<char>(0x80 | 126));
        out.push_back(static_cast<char>((text.size() >> 8) & 0xff));
        out.push_back(static_cast<char>(text.size() & 0xff));
    }
    out.append(reinterpret_cast<char*>(mask), 4);
    for (std::size_t i = 0; i < text.size(); ++i)
        out.push_back(static_cast<char>(text[i] ^ mask[i % 4]));
    return out;
}

} // namespace

class PulseTest : public ::testing::Test {
protected:
    void SetUp() override {
        hub_ = std::make_unique<pulse::Hub>();
        server_ = std::make_unique<Server>();
        server_->Get("/chat", [this](Request& req, Response& res) {
            auto ch = pulse::upgrade(req, res);
            if (!ch.valid()) return;
            {
                std::lock_guard<std::mutex> lk(mu_);
                channel_ = ch;
            }
            hub_->join("lobby", ch);
            ch.on_text([this](pulse::Channel&, std::string_view msg) {
                hub_->to("lobby").broadcast_text(msg);
            });
            ch.send_text("welcome");
        });
        ASSERT_TRUE(server_->Run("127.0.0.1", 0));
        port_ = server_->port();
    }

    void TearDown() override { server_->Stop(); }

    pulse::Channel channel() {
        std::lock_guard<std::mutex> lk(mu_);
        return channel_;
    }

    std::unique_ptr<Server> server_;
    std::unique_ptr<pulse::Hub> hub_;
    uint16_t port_{0};
    std::mutex mu_;
    pulse::Channel channel_;
};

TEST_F(PulseTest, HandshakeAndWelcome) {
    TcpClient c;
    ASSERT_TRUE(c.connect_to(port_));
    ASSERT_TRUE(c.send_all(ws_handshake_("/chat")));

    std::string buf;
    ASSERT_TRUE(c.read_until(buf, [](const std::string& b) {
        return b.find("\r\n\r\n") != std::string::npos;
    }));
    EXPECT_NE(buf.find("101"), std::string::npos);
    EXPECT_NE(buf.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo="), std::string::npos);
    EXPECT_NE(buf.find("Upgrade: websocket"), std::string::npos);

    // Welcome text frame from server (unmasked)
    ASSERT_TRUE(c.read_until(buf, [](const std::string& b) {
        return b.find("welcome") != std::string::npos;
    }));
}

TEST_F(PulseTest, EchoViaHub) {
    TcpClient c;
    ASSERT_TRUE(c.connect_to(port_));
    ASSERT_TRUE(c.send_all(ws_handshake_("/chat")));
    std::string buf;
    ASSERT_TRUE(c.read_until(buf, [](const std::string& b) {
        return b.find("welcome") != std::string::npos;
    }));

    ASSERT_TRUE(c.send_all(mask_text_frame_("ping-msg")));
    ASSERT_TRUE(c.read_until(buf, [](const std::string& b) {
        return b.find("ping-msg") != std::string::npos;
    }));
}

TEST_F(PulseTest, RejectsBadUpgrade) {
    TcpClient c;
    ASSERT_TRUE(c.connect_to(port_));
    ASSERT_TRUE(c.send_all("GET /chat HTTP/1.1\r\nHost: x\r\n\r\n"));
    std::string buf;
    ASSERT_TRUE(c.read_until(buf, [](const std::string& b) {
        return b.find("\r\n\r\n") != std::string::npos;
    }));
    EXPECT_NE(buf.find("400"), std::string::npos);
}
