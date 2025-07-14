#include "server.h"
#include "request.h"
#include "response.h"

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <thread>

namespace socketify {

Server::Server(int port) : port(port) {}

void Server::set_host(const std::string& h) {
    host = h;
}

void Server::enable_ssl(const SSLConfig& config) {
    ssl = config;
    ssl.enabled = true;
}

void Server::get(const std::string& path, RouteHandler handler) {
    router.get(path, handler);
}

void Server::post(const std::string& path, RouteHandler handler) {
    router.post(path, handler);
}

void Server::listen() {
    int server_fd;
    struct sockaddr_in address;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        return;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    if (inet_addr(host.c_str()) == INADDR_NONE) {
        std::cerr << "Invalid IP address\n";
        return;
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(host.c_str());
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        return;
    }

    if (ssl.enabled) {
        std::cout << "[INFO] HTTPS is not yet implemented (planned)\n";
    }

    if (::listen(server_fd, 10) < 0) {
        perror("Listen failed");
        return;
    }

    std::cout << "Server listening on http://" << host << ":" << port << "\n";

    while (true) {
        int addrlen = sizeof(address);
        int client_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }

        std::thread([this, client_socket]() {
            char buffer[4096] = {0};
            int valread = read(client_socket, buffer, sizeof(buffer));
            if (valread > 0) {
                std::string request_data(buffer);
                std::string method = request_data.substr(0, request_data.find(' '));
                std::string path = request_data.substr(request_data.find(' ') + 1);
                path = path.substr(0, path.find(' '));

                HttpMethod http_method = method == "POST" ? HttpMethod::POST : HttpMethod::GET;
                Json req_body;

                size_t body_start = request_data.find("\r\n\r\n");
                if (body_start != std::string::npos) {
                    std::string body = request_data.substr(body_start + 4);
                    try {
                        req_body = Json::parse(body);
                    } catch (...) {
                        // std::cerr << "[WARN] Failed to parse JSON body\n";
                    }
                }

                Request req(http_method, path, req_body);
                Response res;

                auto handler = router.match(http_method, path);
                if (handler) {
                    handler(req, res);
                } else {
                    res.set_status(404);
                    res.set_body({{"error", "Not Found"}});
                }

                std::string response_str = res.to_string();
                send(client_socket, response_str.c_str(), response_str.size(), 0);
            }

            close(client_socket);
        }).detach();
    }
}

} // namespace socketify