// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <unordered_map>

namespace socketify {

enum class HttpMethod { Get, Post, Put, Patch, Delete_, Options, Head, Any };

using Headers = std::unordered_map<std::string, std::string>;
using Params  = std::unordered_map<std::string, std::string>;
using Query   = std::unordered_map<std::string, std::string>;

} // namespace socketify
