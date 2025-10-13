#include "socketify/logging.h"
#include "socketify/request.h"
#include "socketify/response.h"

#include <fmt/core.h>
#include <fmt/chrono.h>
#include <chrono>

namespace socketify {

Middleware CreateLogger() {
    return [](Request& req, Response& res, Next next) {
        auto start_time = std::chrono::steady_clock::now();

        next();

        auto duration = std::chrono::steady_clock::now() - start_time;
        double ms = std::chrono::duration<double, std::milli>(duration).count();
        auto now = std::chrono::system_clock::now();

        fmt::print("[{:%Y-%m-%d %H:%M:%S}] \"{} {} {}\" {} - {:.2f} ms\n",
                   now,
                   to_string(req.method()),
                   req.raw_target(),
                   req.http_version(),
                   res.status_code(),
                   ms);
    };
}

} // namespace socketify