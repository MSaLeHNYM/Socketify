

#include "socketify/server.h"
#include "socketify/cors.h"
#include <iostream>

using namespace socketify;

int main()
{

    Server server;
    server.Use(socketify::cors::middleware({
        .allow_origin = "*",     // or exact domain; if with credentials, prefer .reflect_origin
        .reflect_origin = false, // true if you want to mirror Origin dynamically
        .allow_methods = "GET,POST",
        .allow_headers = "", // echo requested if empty
        .expose_headers = "X-My-Header",
        .allow_credentials = false, // true only if you trust the site; cannot use "*" then
        .max_age_seconds = 600,
        .allow_private_network = false,
        .preflight_continue = false // auto 204 for OPTIONS
    }));

    
    // Simple GET /hello route
    server.AddRoute(Method::GET, "/hello", [](Request &req, Response &res)
                    { res.html("<h1>Hello, World!</h1>"); });

    server.AddRoute(Method::POST, "/pp", [](Request &req, Response &res)
                    { res.send("This is Post"); });

    // Default root
    server.AddRoute(Method::GET, "/", [](Request &req, Response &res)
                    { res.send("Welcome to Socketify!"); });

    // Start server on port 8080
    if (!server.Run("0.0.0.0", 80))
    {
        return 1;
    }
    else
    {
        std::cout << "Server Start in http://0.0.0.0:80" << std::endl;
    }

    // Block main thread (simple)
    for (;;)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
