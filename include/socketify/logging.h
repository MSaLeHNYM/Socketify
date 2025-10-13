#pragma once
// socketify/logging.h — Request logging middleware

#include "middleware.h"

namespace socketify {

// Creates a simple request logger middleware.
//
// Format:
// [timestamp] "METHOD /path HTTP/version" status_code - response_time_ms ms
//
// Example:
// [2023-10-27 10:30:00] "GET /api/users HTTP/1.1" 200 - 5.25 ms
Middleware CreateLogger();

} // namespace socketify