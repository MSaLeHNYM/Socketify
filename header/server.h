#pragma once

#include "route.h"
#include "https.h"

namespace socketify {

class Server {
    std::string host = "0.0.0.0";
    int port = 8080;
    Router router;
    SSLConfig ssl;

public:
    Server(int port);
    void set_host(const std::string& host);
    void enable_ssl(const SSLConfig& config);

    void get(const std::string& path, RouteHandler handler);
    void post(const std::string& path, RouteHandler handler);

    void listen();
};

}
