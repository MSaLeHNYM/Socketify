#include "socketify/cors.h"

#include <algorithm>

namespace socketify::cors {

static inline char ascii_lower(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc >= 'A' && uc <= 'Z') return static_cast<char>(uc - 'A' + 'a');
    return static_cast<char>(uc);
}
static inline std::string to_lower(std::string s) {
    for (auto& c : s) c = ascii_lower(c);
    return s;
}

static inline std::string_view get_header(const HeaderMap& h, std::string_view key) {
    auto it = h.find(std::string(key));
    if (it == h.end()) return {};
    return it->second;
}

static inline void append_vary(HeaderMap& h, std::string_view token) {
    auto it = h.find("Vary");
    if (it == h.end()) {
        h.emplace("Vary", std::string(token));
        return;
    }
    // append if not already present (case-insensitive contains)
    std::string cur = it->second;
    std::string low = to_lower(cur);
    std::string needle = to_lower(std::string(token));
    if (low.find(needle) == std::string::npos) {
        cur.append(", ").append(std::string(token));
        it->second = std::move(cur);
    }
}

static inline void set_header(Response& res, std::string_view k, std::string_view v) {
    res.set_header(k, v);
}

static inline bool is_preflight(const Request& req) {
    if (req.method() != Method::OPTIONS) return false;
    // CORS preflight must include Access-Control-Request-Method
    return !get_header(req.headers(), "Access-Control-Request-Method").empty();
}

static inline bool origin_allowed(const std::string& request_origin,
                                  const CorsOptions& o,
                                  std::string& out_value,
                                  bool& set_vary_origin) {
    set_vary_origin = false;

    if (request_origin.empty()) return false; // no CORS
    if (o.allow_origin == "*") {
        if (!o.allow_credentials) {
            out_value = "*";
            return true;
        }
        // With credentials, browsers reject wildcard. We’ll reflect origin if requested.
        if (o.reflect_origin) {
            out_value = request_origin;
            set_vary_origin = true;
            return true;
        }
        // If not reflect, and credentials requested, we cannot safely set "*".
        // Return false so no header is sent; browser will block.
        return false;
    }

    if (o.reflect_origin) {
        out_value = request_origin;
        set_vary_origin = true;
        return true;
    }

    // exact string allow_origin
    out_value = o.allow_origin;
    return true;
}

/**
 * @brief Creates a new CORS middleware.
 * @param opts The CORS options.
 * @return A middleware function.
 */
Middleware middleware(CorsOptions opts) {
    return [opts](Request& req, Response& res, Next next) {
        const auto origin = std::string(get_header(req.headers(), "Origin"));
        if (origin.empty()) {
            // Not a CORS request; proceed
            next();
            return;
        }

        bool vary_origin = false;
        std::string allow_origin_value;
        const bool allowed = origin_allowed(origin, opts, allow_origin_value, vary_origin);

        if (allowed) {
            set_header(res, "Access-Control-Allow-Origin", allow_origin_value);
            if (opts.allow_credentials) {
                set_header(res, "Access-Control-Allow-Credentials", "true");
            }
            if (vary_origin) {
                // Reflecting origin → add Vary: Origin
                auto& h = const_cast<HeaderMap&>(res.headers());
                append_vary(h, "Origin");
            }
        }

        if (is_preflight(req)) {
            // Preflight response headers
            // Methods
            auto req_method = get_header(req.headers(), "Access-Control-Request-Method");
            if (!opts.allow_methods.empty()) {
                set_header(res, "Access-Control-Allow-Methods", opts.allow_methods);
            } else if (!req_method.empty()) {
                set_header(res, "Access-Control-Allow-Methods", req_method);
            }

            // Requested headers
            auto req_headers = get_header(req.headers(), "Access-Control-Request-Headers");
            if (!opts.allow_headers.empty()) {
                set_header(res, "Access-Control-Allow-Headers", opts.allow_headers);
            } else if (!req_headers.empty()) {
                // echo requested headers
                set_header(res, "Access-Control-Allow-Headers", req_headers);
                // since we reflect request-specified headers, this varies on that header
                auto& h = const_cast<HeaderMap&>(res.headers());
                append_vary(h, "Access-Control-Request-Headers");
            }

            // Private Network Access (Chrome)
            if (opts.allow_private_network) {
                auto pna = get_header(req.headers(), "Access-Control-Request-Private-Network");
                if (!pna.empty()) {
                    // If header present, add allow header (many browsers treat any value as signal)
                    set_header(res, "Access-Control-Allow-Private-Network", "true");
                }
            }

            if (opts.max_age_seconds > 0) {
                set_header(res, "Access-Control-Max-Age", std::to_string(opts.max_age_seconds));
            }

            if (!allowed) {
                // If origin is not allowed, do not set CORS headers (already limited),
                // and end with 204 to stop the browser from retrying (common practice).
                // Alternatively, you could send 403; but most frameworks simply omit headers.
                if (!opts.preflight_continue) {
                    res.status(Status::NoContent);
                    res.set_header(H_ContentLength, "0");
                    res.end();
                    return;
                }
                next();
                return;
            }

            // Short-circuit preflight unless user wants to continue
            if (!opts.preflight_continue) {
                res.status(Status::NoContent);
                res.set_header(H_ContentLength, "0");
                // text/plain makes some proxies happy; safe with empty body
                res.set_header(H_ContentType, "text/plain; charset=utf-8");
                res.end();
                return;
            }

            next();
            return;
        }

        // Actual request:
        if (!opts.expose_headers.empty()) {
            set_header(res, "Access-Control-Expose-Headers", opts.expose_headers);
        }

        // If origin not allowed, we just don't set CORS headers; browser will block.
        next();
    };
}

} // namespace socketify::cors