// SPDX-License-Identifier: MIT
#pragma once
#include "http.h"
#include "common.h"

// nlohmann single-header:
#include "json.hpp"  // make sure your include path points to header/json.hpp

#include <any>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace socketify {

struct Request {
    HttpMethod      method{HttpMethod::Get};
    std::string     path;            // full path
    std::string     route_pattern;   // matched pattern
    Headers         headers;
    Query           query;
    Params          params;
    std::string     ip;
    std::string     scheme{"http"};
    std::string     host;
    std::string     content_type;
    std::size_t     content_length{0};

    // body APIs (implemented in .cpp; respect body limits)
    // - body() buffers and returns the request body (once).
    // - json() parses via nlohmann::json when content-type is JSON.
    std::string body();
    std::optional<nlohmann::json> json();

    bool body_too_large{false}; // set by impl when size limits exceeded

    // per-request bag
    template<class T> void set(std::string key, T value) {
        bag_[std::move(key)] = std::move(value);
    }
    template<class T> T* get(const std::string& key) {
        auto it = bag_.find(key);
        if (it == bag_.end()) return nullptr;
        return std::any_cast<T>(&it->second);
    }

private:
    std::unordered_map<std::string, std::any> bag_;
};

} // namespace socketify
