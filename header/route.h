// SPDX-License-Identifier: MIT
#pragma once
#include "http.h"
#include "request.h"
#include "response.h"

#include <functional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace socketify {

// Define Handler here so this header doesn't depend on server.h
using Handler = std::function<void(Request&, Response&)>;

struct Route {
    HttpMethod method;
    std::string pattern;                // e.g. "/users/:id"
    std::regex  re;                     // compiled regex
    std::vector<std::string> param_names;
    Handler     handler;
};

class Router {
public:
    void add(HttpMethod m, const std::string& pattern, Handler h);
    // returns true if matched/handled
    bool dispatch(Request& req, Response& res) const;

private:
    std::vector<Route> routes_;
};

} // namespace socketify
