#pragma once

#include "common.h"

namespace socketify {

class Request {
public:
    HttpMethod method;
    std::string path;
    Json body;

    Request(HttpMethod m, const std::string& p, const Json& b = {})
        : method(m), path(p), body(b) {}
};

}
