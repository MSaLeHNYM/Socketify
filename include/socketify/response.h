#pragma once
// socketify/response.h — HTTP response builder

#include "socketify/http.h"
#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace socketify {

/**
 * @brief Represents an outgoing HTTP response.
 */
class Response {
public:
    Response() = default;

    // ---- Status / headers ----
    /**
     * @brief Sets the HTTP status code.
     * @param s The status code enum.
     * @return A reference to the Response object.
     */
    Response& status(Status s) noexcept { status_code_ = static_cast<std::uint16_t>(s); return *this; }
    /**
     * @brief Sets the HTTP status code.
     * @param code The status code.
     * @return A reference to the Response object.
     */
    Response& status(std::uint16_t code) noexcept { status_code_ = code; return *this; }

    /**
     * @brief Sets a header field.
     * @param key The header key.
     * @param value The header value.
     * @return A reference to the Response object.
     */
    Response& set_header(std::string_view key, std::string_view value);
    /**
     * @brief Sets the Content-Type header.
     * @param ct The content type.
     * @return A reference to the Response object.
     */
    Response& set_content_type(std::string_view ct) { return set_header(H_ContentType, ct); }

    // ---- Cookies (simple for v1; full cookie struct can come later) ----
    /**
     * @brief Sets a cookie.
     * @param cookie_line The full cookie string.
     * @return A reference to the Response object.
     */
    Response& set_cookie(std::string_view cookie_line);

    // ---- Send helpers (buffered for now) ----
    /**
     * @brief Sends a response with a body.
     * @param body The response body.
     * @param content_type The content type of the body.
     * @return true if the response was sent successfully, false otherwise.
     */
    bool send(std::string_view body, std::string_view content_type = "text/plain; charset=utf-8");
    /**
     * @brief Sends an HTML response.
     * @param html The HTML content.
     * @return true if the response was sent successfully, false otherwise.
     */
    bool html(std::string_view html) { return send(html, "text/html; charset=utf-8"); }
    /**
     * @brief Sends a JSON response.
     * @param j The JSON object.
     * @return true if the response was sent successfully, false otherwise.
     */
    bool json(const nlohmann::json& j);

    // Stream-style writing (accumulates; mark finished with end()).
    // Returns false if already ended.
    /**
     * @brief Writes a chunk of data to the response body.
     * @param chunk The data to write.
     * @return true if the write was successful, false otherwise.
     */
    bool write(std::string_view chunk);
    /**
     * @brief Finalizes the response.
     */
    void end();                    // finalize the response (no more writes)
    /**
     * @brief Tries to write a chunk of data to the response body.
     * @param chunk The data to write.
     * @return true if the write was successful, false otherwise.
     */
    bool tryWrite(std::string_view chunk) { return ended_ ? false : write(chunk); }

    // Redirection helper
    /**
     * @brief Redirects the client to a different URL.
     * @param url The URL to redirect to.
     * @param code The HTTP status code for the redirection.
     */
    void redirect(std::string_view url, std::uint16_t code = 302);

    // File sending (stub for now; will use detail::file_io later)
    /**
     * @brief Sends a file as the response.
     * @param fs_path The path to the file.
     * @param download Whether to force a download.
     * @param download_name The name to use for the downloaded file.
     * @return true if the file was sent successfully, false otherwise.
     */
    bool send_file(std::string_view fs_path, bool /*download*/ = false,
                   std::string_view /*download_name*/ = "");

    // ---- Introspection for server internals ----
    /**
     * @brief Checks if the response has been ended.
     * @return true if the response has been ended, false otherwise.
     */
    bool ended() const noexcept { return ended_; }
    /**
     * @brief Gets the HTTP status code.
     * @return The status code.
     */
    std::uint16_t status_code() const noexcept { return status_code_; }
    /**
     * @brief Gets the response headers.
     * @return A const reference to the header map.
     */
    const HeaderMap& headers() const noexcept { return headers_; }
    /**
     * @brief Gets a view of the response body.
     * @return A string view of the body.
     */
    std::string_view body_view() const noexcept { return body_; }
    /**
     * @brief Gets the response body as a string.
     * @return The body as a string.
     */
    const std::string& body_string() const noexcept { return body_storage_; }
    /**
     * @brief Checks if the response has a body.
     * @return true if the response has a body, false otherwise.
     */
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