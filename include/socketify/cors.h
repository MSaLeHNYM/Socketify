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

/**
 * @brief Holds configuration options for CORS middleware.
 */
struct CorsOptions {
    /**
     * @brief The value for the Access-Control-Allow-Origin header.
     */
    std::string allow_origin{"*"};
    /**
     * @brief Whether to reflect the request's Origin header.
     */
    bool reflect_origin{false};

    /**
     * @brief The value for the Access-Control-Allow-Methods header.
     */
    std::string allow_methods{"GET,POST,PUT,PATCH,DELETE,OPTIONS,HEAD"};

    /**
     * @brief The value for the Access-Control-Allow-Headers header.
     */
    std::string allow_headers{};

    /**
     * @brief The value for the Access-Control-Expose-Headers header.
     */
    std::string expose_headers{};

    /**
     * @brief Whether to set the Access-Control-Allow-Credentials header.
     */
    bool allow_credentials{false};

    /**
     * @brief The value for the Access-Control-Max-Age header.
     */
    int max_age_seconds{600};

    /**
     * @brief Whether to allow Chrome's Private Network Access preflights.
     */
    bool allow_private_network{false};

    /**
     * @brief Whether to continue to the next middleware after a preflight request.
     */
    bool preflight_continue{false};
};

/**
 * @brief Creates a new CORS middleware.
 * @param opts The CORS options.
 * @return A middleware function.
 */
Middleware middleware(CorsOptions opts = {});

} // namespace socketify::cors