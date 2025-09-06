#include "common.h"
#include <random>
#include <sstream>

namespace socketify {

std::string generate_request_id() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    auto v = rng();
    std::ostringstream oss;
    oss << std::hex << v;
    return oss.str();
}

} // namespace socketify
