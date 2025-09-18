#include "socketify/server.h"

#include <iostream>

using namespace socketify;

int main()
{
    ServerOptions opts;
    Server server(opts);

    // Simple GET /hello route
    server.AddRoute(Method::GET, "/hello", [](Request &req, Response &res)
                    { res.html("<h1>Hello, World!</h1>"); });

    server.AddRoute(Method::POST, "/pp", [](Request &req, Response &res){
        res.send("This is Post");
    });


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
