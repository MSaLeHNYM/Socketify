/**
 * @file middleware.cpp
 * @brief Built-in middleware: request_id, body_limit.
 */

#include "socketify/middleware.h"
#include "socketify/detail/utils.h"

namespace socketify::middleware {

Middleware request_id() {
    return [](Request& req, Response& res, Next next) {
        std::string id{req.header(H_XRequestId)};
        if (id.empty()) {
            id = detail::random_token(8);
            req.mutable_headers()[std::string(H_XRequestId)] = id;
        }
        res.set_header(H_XRequestId, id);
        next();
    };
}

Middleware body_limit(std::size_t max_bytes) {
    return [max_bytes](Request& req, Response& res, Next next) {
        if (req.body_view().size() > max_bytes) {
            res.status(Status::PayloadTooLarge).send("Payload Too Large\n");
            return;
        }
        next();
    };
}

} // namespace socketify::middleware
