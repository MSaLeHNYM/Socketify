#include "socketify/detail/socket.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring> // for strerror
#include <iostream> // for cerr

namespace socketify::detail {

NativeSocket create_listening_socket(std::string_view host, uint16_t port, int backlog) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC; // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // For wildcard IP address

    addrinfo* result;
    std::string port_str = std::to_string(port);
    if (getaddrinfo(std::string(host).c_str(), port_str.c_str(), &hints, &result) != 0) {
        std::cerr << "getaddrinfo failed\n";
        return kInvalidSocket;
    }

    NativeSocket listen_fd = kInvalidSocket;
    for (addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
        listen_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (listen_fd == kInvalidSocket) continue;

        int yes = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        if (bind(listen_fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break; // Success
        }

        close(listen_fd);
        listen_fd = kInvalidSocket;
    }
    freeaddrinfo(result);

    if (listen_fd == kInvalidSocket) {
        std::cerr << "Could not bind to any address\n";
        return kInvalidSocket;
    }

    if (listen(listen_fd, backlog) != 0) {
        std::cerr << "listen failed: " << strerror(errno) << "\n";
        close(listen_fd);
        return kInvalidSocket;
    }

    set_non_blocking(listen_fd);
    return listen_fd;
}

NativeSocket accept_connection(NativeSocket listen_fd) {
    return accept(listen_fd, nullptr, nullptr);
}

void close_socket(NativeSocket fd) {
    if (fd != kInvalidSocket) {
        close(fd);
    }
}

bool set_non_blocking(NativeSocket fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

ssize_t read_socket(NativeSocket fd, void* buf, size_t count) {
    return read(fd, buf, count);
}

ssize_t write_socket(NativeSocket fd, const void* buf, size_t count) {
    return write(fd, buf, count);
}

bool write_all(NativeSocket fd, std::string_view data) {
    const char* p = data.data();
    size_t remaining = data.size();
    while (remaining > 0) {
        ssize_t written = write(fd, p, remaining);
        if (written <= 0) {
            // EAGAIN/EWOULDBLOCK means try again later, but for this simple
            // blocking function, we treat it as an error.
            return false;
        }
        p += written;
        remaining -= written;
    }
    return true;
}

} // namespace socketify::detail