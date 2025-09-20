#pragma once
// socketify/cors.h — CORS middleware (preflight + simple requests)

#include "socketify/http.h"
#include "socketify/request.h"
#include "socketify/response.h"
#include "socketify/router.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace socketify::cors {

struct CorsOptions {
    // Allow origin policy:
    //  - "*" (wildcard): sets Access-Control-Allow-Origin: * (not with credentials)
    //  - exact string (e.g., "https://example.com")
    //  - if reflect_origin == true: mirrors request Origin and adds Vary: Origin
    std::string allow_origin{"*"};
    bool reflect_origin{false};

    // Comma-separated method names for preflight response (default common set).
    // If empty, we’ll echo the Access-Control-Request-Method header.
    std::string allow_methods{"GET,POST,PUT,PATCH,DELETE,OPTIONS,HEAD"};

    // Comma-separated headers allowed in preflight.
    // If empty, we’ll echo Access-Control-Request-Headers.
    std::string allow_headers{};

    // Comma-separated response headers exposed to browsers on simple/actual requests.
    std::string expose_headers{};

    // Whether to set Access-Control-Allow-Credentials: true.
    bool allow_credentials{false};

    // Max-Age (seconds) for caching preflight in browser.
    // Set 0 to omit the header.
    int max_age_seconds{600};

    // Support Chrome’s Private Network Access preflights.
    bool allow_private_network{false};

    // If true, we do NOT short-circuit preflight; we just set headers and call next().
    // If false (default), we end preflights with 204 No Content.
    bool preflight_continue{false};
};

// Factory producing a Middleware you can `Use(...)`
Middleware middleware(CorsOptions opts = {});

} // namespace socketify::cors
