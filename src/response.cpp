#include "response.h"

namespace socketify {

void Response::set_status(int code) {
    status_code = code;
}

void Response::set_body(const Json& body) {
    response_body = body;
}

std::string Response::to_string() const {
    std::string body_str = response_body.dump();
    return
        "HTTP/1.1 " + std::to_string(status_code) + " OK\r\n" +
        "Content-Type: application/json\r\n" +
        "Content-Length: " + std::to_string(body_str.size()) + "\r\n\r\n" +
        body_str;
}

}
