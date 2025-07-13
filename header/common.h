#pragma once

#include <string>
#include <functional>
#include <unordered_map>
#include "json.hpp"

namespace socketify {
    enum class HttpMethod { GET, POST };
    using Json = nlohmann::json;
}
