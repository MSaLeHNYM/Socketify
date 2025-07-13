#include <socketify/server.h>
#include <socketify/request.h>
#include <socketify/response.h>

#include <iostream>
#include <nlohmann/json.hpp>

int main() {
    socketify::Server server(8080);
    server.set_host("127.0.0.1");

    server.get("/", [](const socketify::Request& req, socketify::Response& res) {
        nlohmann::json msg = {
            {"message", "Hello from GET!"}
        };
        res.set_status(200);
        res.set_body(msg);
    });

    server.post("/data", [](const socketify::Request& req, socketify::Response& res) {
        nlohmann::json response = {
            {"received", req.body}
        };
        res.set_status(200);
        res.set_body(response);
    });

    server.listen();
    return 0;
}
