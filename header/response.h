#pragma once

#include "common.h"

namespace socketify {

class Response {
    int status_code = 200;
    Json response_body;

public:
    void set_status(int code);
    void set_body(const Json& body);
    std::string to_string() const;
};

}
