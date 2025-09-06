#include "route.h"
#include <regex>
#include <cctype>   // for std::isalnum

namespace socketify {

void Router::add(HttpMethod m, const std::string& pattern, Handler h) {
    // Convert "/users/:id/books/:bookId" -> regex with captures
    std::string re_str = "^";
    std::vector<std::string> names;

    for (size_t i = 0; i < pattern.size();) {
        if (pattern[i] == ':') {
            size_t j = i + 1;
            while (j < pattern.size() && (std::isalnum(static_cast<unsigned char>(pattern[j])) || pattern[j] == '_')) {
                ++j;
            }
            names.push_back(pattern.substr(i + 1, j - (i + 1)));
            re_str += "([^/]+)";
            i = j;
        } else {
            // escape regex special chars
            char c = pattern[i++];
            switch (c) {
                case '.': case '^': case '$': case '|': case '(': case ')':
                case '[': case ']': case '{': case '}': case '*': case '+': case '?': case '\\':
                    re_str += '\\';
                    // fallthrough
                default:
                    re_str += c;
            }
        }
    }
    re_str += "$";

    Route r;
    r.method = m;
    r.pattern = pattern;
    r.re = std::regex(re_str);
    r.param_names = std::move(names);
    r.handler = std::move(h);

    routes_.push_back(std::move(r));
}

bool Router::dispatch(Request& req, Response& res) const {
    for (const auto& r : routes_) {
        if (r.method != HttpMethod::Any && r.method != req.method) continue;

        std::smatch m;
        if (std::regex_match(req.path, m, r.re)) {
            // fill path params
            for (size_t i = 0; i < r.param_names.size(); ++i) {
                req.params[r.param_names[i]] = m[i + 1];
            }
            req.route_pattern = r.pattern;

            r.handler(req, res);
            return true;
        }
    }
    return false;
}

} // namespace socketify
