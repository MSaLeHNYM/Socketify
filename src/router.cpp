#include "socketify/router.h"

#include <algorithm>
#include <cassert>

namespace socketify {

// ---------- Router helpers ----------
static inline char ascii_lower(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc >= 'A' && uc <= 'Z') return static_cast<char>(uc - 'A' + 'a');
    return static_cast<char>(uc);
}
static inline bool iequal_ascii(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (ascii_lower(a[i]) != ascii_lower(b[i])) return false;
    }
    return true;
}

std::vector<Route::Seg> Router::compile_pattern_(std::string_view pattern) {
    std::vector<Route::Seg> segs;
    if (pattern.empty()) {
        // Treat empty as "/"
        return segs; // empty segs means root
    }

    auto parts = split_path_(pattern);
    segs.reserve(parts.size());

    for (auto part : parts) {
        if (part.empty()) continue;
        if (part.front() == ':') {
            segs.push_back({Route::Seg::Param, std::string(part.substr(1))});
        } else if (part.front() == '*') {
            segs.push_back({Route::Seg::Wildcard, std::string(part.substr(1))});
            break; // wildcard eats the rest
        } else {
            segs.push_back({Route::Seg::Static, std::string(part)});
        }
    }
    return segs;
}

std::vector<std::string_view> Router::split_path_(std::string_view s) {
    std::vector<std::string_view> out;
    size_t start = 0;
    while (start < s.size()) {
        size_t pos = s.find('/', start);
        if (pos == std::string_view::npos) {
            if (start < s.size()) out.emplace_back(s.substr(start));
            break;
        } else {
            if (pos > start) out.emplace_back(s.substr(start, pos - start));
            start = pos + 1;
        }
    }
    return out; // "/" -> empty vector (root)
}

bool Router::starts_with_(std::string_view s, std::string_view pfx) {
    return s.size() >= pfx.size() && std::equal(pfx.begin(), pfx.end(), s.begin());
}

// Match `path` against compiled `segs`. If matched, fill req.params().
bool Router::match_and_bind_(std::string_view path,
                             const std::vector<Route::Seg>& segs,
                             Request& req) {
    // Root pattern (empty segs) matches only "/" or "".
    if (segs.empty()) {
        return path == "/" || path.empty();
    }

    auto parts = split_path_(path);
    auto& params = const_cast<ParamMap&>(req.params());
    params.clear();

    size_t i = 0, j = 0;
    while (i < parts.size() && j < segs.size()) {
        const auto& seg = segs[j];
        switch (seg.kind) {
            case Route::Seg::Static:
                if (!iequal_ascii(parts[i], seg.text)) return false;
                ++i; ++j;
                break;
            case Route::Seg::Param:
                params[seg.text] = std::string(parts[i]);
                ++i; ++j;
                break;
            case Route::Seg::Wildcard: {
                std::string rest;
                for (; i < parts.size(); ++i) {
                    if (!rest.empty()) rest.push_back('/');
                    rest.append(parts[i].data(), parts[i].size());
                }
                params[seg.text] = std::move(rest);
                ++j;
                i = parts.size();
                break;
            }
        }
    }

    if (j < segs.size()) {
        if (!(segs[j].kind == Route::Seg::Wildcard && j + 1 == segs.size())) {
            return false;
        }
        params[segs[j].text] = "";
        ++j;
    }
    return i == parts.size() && j == segs.size();
}

// ---------- Router public API ----------
bool Router::dispatch(Request& req, Response& res) const {
    // Build a chain of GLOBAL middleware only, and let a terminal lambda
    // perform route lookup + per-route middleware + handler.
    size_t idx = 0;

    std::function<void()> next;
    next = [&]() {
        if (res.ended()) return;

        if (idx < global_mw_.size()) {
            auto& mw = global_mw_[idx++];
            mw(req, res, next);
            return;
        }

        // ---- Terminal stage: do routing now ----
        const Method method = req.method();
        const std::string_view path = req.path();

        const Route* matched = nullptr;

        bool path_matched_any = false;
        std::vector<Method> allowed;

        for (const auto& r : routes_) {
            Request temp = req;
            if (!match_and_bind_(path, r.segs_, temp)) continue;

            path_matched_any = true;

            if (r.method() == Method::ANY || r.method() == method) {
                const_cast<ParamMap&>(req.params()) = const_cast<ParamMap&>(temp.params());
                matched = &r;
                break;
            } else {
                if (r.method() != Method::UNKNOWN && r.method() != Method::ANY) {
                    allowed.push_back(r.method());
                }
            }
        }

        if (!matched) {
            if (path_matched_any) {
                std::sort(allowed.begin(), allowed.end(), [](Method a, Method b){ return static_cast<int>(a) < static_cast<int>(b); });
                allowed.erase(std::unique(allowed.begin(), allowed.end()), allowed.end());

                if (std::find(allowed.begin(), allowed.end(), Method::GET) != allowed.end() &&
                    std::find(allowed.begin(), allowed.end(), Method::HEAD) == allowed.end()) {
                    allowed.push_back(Method::HEAD);
                }

                std::string allow_header;
                for (size_t i = 0; i < allowed.size(); ++i) {
                    if (i) allow_header.append(", ");
                    allow_header.append(std::string(to_string(allowed[i])));
                }

                res.status(Status::MethodNotAllowed)
                   .set_header("Allow", allow_header)
                   .send("Method Not Allowed\n");
                return;
            }
            // no route at all → let caller send 404
            return;
        }

        // Build chain: group MWs + route MWs + final handler
        std::vector<Middleware> chain;
        for (const auto& g : groups_) {
            if (starts_with_(matched->pattern(), g.prefix())) {
                for (const auto& mw : g.middlewares()) chain.push_back(mw);
            }
        }
        for (const auto& mw : matched->middlewares()) chain.push_back(mw);

        chain.push_back([matched](Request& rq, Response& rs, Next){
            matched->handler()(rq, rs);
        });

        // Execute per-route chain
        size_t j = 0;
        std::function<void()> step;
        step = [&]() {
            if (j >= chain.size() || res.ended()) return;
            auto& mw = chain[j++];
            mw(req, res, step);
        };
        step();
    };

    // Kick off global chain
    next();

    // Return true if response ended (middleware or route handled)
    return res.ended();
}

} // namespace socketify
