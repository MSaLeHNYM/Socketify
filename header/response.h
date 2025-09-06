// SPDX-License-Identifier: MIT
#pragma once
#include "http.h"
#include "common.h"

// nlohmann single-header:
#include "json.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace socketify {

class Response {
public:
    Response& status_code(int code);
    Response& set_header(std::string_view k, std::string_view v);

    Response& set_cookie(std::string_view name,
                         std::string_view val,
                         std::chrono::seconds max_age = std::chrono::seconds{0},
                         bool http_only = true,
                         bool secure = true,
                         std::string_view same_site = "Lax",
                         std::optional<std::string_view> path = std::nullopt,
                         std::optional<std::string_view> domain = std::nullopt);

    // Send helpers
    void text (std::string_view s, int code = 200);
    void bytes(const void* data, std::size_t n, int code = 200);
    void file (const std::filesystem::path& p,
               std::optional<std::string_view> download_as = std::nullopt);
    void redirect(std::string_view location, int code = 302);

    // JSON helper using nlohmann/json
    void json(const nlohmann::json& j, int code = 200) {
        auto dumped = j.dump(); // default, user can pre-dump if they want pretty
        set_header("Content-Type", "application/json; charset=utf-8");
        text(dumped, code);
    }

    // streaming
    void write(std::string_view chunk);
    void end();

    bool committed() const noexcept { return committed_; }
    int  status() const noexcept { return status_; }
    const Headers& headers() const noexcept { return headers_; }

private:
    int     status_{200};
    Headers headers_;
    bool    committed_{false};
};

} // namespace socketify
