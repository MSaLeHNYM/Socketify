#include <socketify/server.h>
#include <socketify/request.h>
#include <socketify/response.h>

#include <iostream>

int main() {
    socketify::Server server(80);
    server.set_host("0.0.0.0");

    server.get("/", [](const socketify::Request& req, socketify::Response& res) {
        nlohmann::json msg = {
            {"message", "This Is Cool Message"}
        };
        res.set_status(200);
        res.set_body(msg);
    });

    server.listen();
    return 0;
}
