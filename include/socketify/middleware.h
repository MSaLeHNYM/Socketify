#pragma once
// socketify/middleware.h — middleware handlers

#include "request.h"
#include "response.h"

#include <functional>

namespace socketify {

using Next = std::function<void()>;
// A middleware is a function that processes a request.
// It can choose to end the response, or pass to the next handler.
//
// - req: The incoming request.
// - res: The response object to populate.
// - next: A function to call to pass control to the next middleware.
using Middleware = std::function<void(Request& req, Response& res, Next next)>;

} // namespace socketify