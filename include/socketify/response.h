#pragma once
// socketify/response.h — HTTP response builder

#include "socketify/http.h"
#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace socketify {

class Response {
public:
    Response() = default;

    // ---- Status / headers ----
    Response& status(Status s) noexcept { status_code_ = static_cast<std::uint16_t>(s); return *this; }
    Response& status(std::uint16_t code) noexcept { status_code_ = code; return *this; }

    Response& set_header(std::string_view key, std::string_view value);
    Response& set_content_type(std::string_view ct) { return set_header(H_ContentType, ct); }

    // ---- Cookies (simple for v1; full cookie struct can come later) ----
    // Adds a Set-Cookie header line. Pass full cookie string ("k=v; Path=/; HttpOnly")
    Response& set_cookie(std::string_view cookie_line);

    // ---- Send helpers (buffered for now) ----
    bool send(std::string_view body, std::string_view content_type = "text/plain; charset=utf-8");
    bool html(std::string_view html) { return send(html, "text/html; charset=utf-8"); }
    bool json(const nlohmann::json& j);

    // Stream-style writing (accumulates; mark finished with end()).
    // Returns false if already ended.
    bool write(std::string_view chunk);
    void end();                    // finalize the response (no more writes)
    bool tryWrite(std::string_view chunk) { return ended_ ? false : write(chunk); }

    // Redirection helper
    void redirect(std::string_view url, std::uint16_t code = 302);

    // File sending (stub for now; will use detail::file_io later)
    bool send_file(std::string_view fs_path, bool /*download*/ = false,
                   std::string_view /*download_name*/ = "");

    // ---- Introspection for server internals ----
    bool ended() const noexcept { return ended_; }
    std::uint16_t status_code() const noexcept { return status_code_; }
    const HeaderMap& headers() const noexcept { return headers_; }
    std::string_view body_view() const noexcept { return body_; }
    const std::string& body_string() const noexcept { return body_storage_; }
    bool has_body() const noexcept { return !body_.empty() || !body_storage_.empty(); }

    // Internal: server will replace the output sink later
    // For v1 skeleton, we buffer in memory and leave flushing to the server.
private:
    void ensure_body_owned_();

    std::uint16_t status_code_{static_cast<std::uint16_t>(Status::OK)};
    HeaderMap     headers_{};

    bool          ended_{false};

    std::string   body_storage_{};   // owns data when we build body
    std::string_view body_{};        // view into body_storage_ or external buf
};

} // namespace socketify
