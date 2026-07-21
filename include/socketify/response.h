#pragma once
/**
 * @file response.h
 * @brief HTTP response builder passed to handlers and middleware.
 *
 * Three kinds of responses are supported:
 *  - **Buffered** (default): body accumulated in memory via send()/json()/write().
 *  - **File**: send_file() streams a file from disk (sendfile(2) on plain sockets).
 *  - **Stream** (SSE): the connection stays open and data is pushed later.
 *  - **Pulse**: bidirectional WebSocket-compatible channel after HTTP 101.
 */

#include "socketify/cookies.h"
#include "socketify/http.h"
#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace socketify {

/**
 * @brief An outgoing HTTP response.
 *
 * @code
 * res.status(Status::Created).json({{"id", 42}});
 * res.html("<h1>hi</h1>");
 * res.send_file("assets/logo.png");
 * res.redirect("/login");
 * @endcode
 */
class Response {
public:
    /** @brief Internal response kind (introspected by the server). */
    enum class Kind : std::uint8_t { Buffered, File, Stream, Pulse };

    Response() = default;

    // ---- Status / headers ----

    /** @brief Set the status from the Status enum. */
    Response& status(Status s) noexcept { status_code_ = static_cast<std::uint16_t>(s); return *this; }
    /** @brief Set the status from a raw code. */
    Response& status(std::uint16_t code) noexcept { status_code_ = code; return *this; }

    /** @brief Set (replace) a response header. */
    Response& set_header(std::string_view key, std::string_view value);
    /** @brief Shortcut for set_header("Content-Type", ct). */
    Response& set_content_type(std::string_view ct) { return set_header(H_ContentType, ct); }

    // ---- Cookies ----

    /**
     * @brief Add a Set-Cookie header from a raw cookie line
     *        ("k=v; Path=/; HttpOnly"). Each call emits its own header.
     */
    Response& set_cookie(std::string_view cookie_line);

    /** @brief Add a Set-Cookie header from a Cookie builder. */
    Response& set_cookie(const Cookie& cookie) { return set_cookie(cookie.to_string()); }

    /** @brief Ask the browser to delete cookie @p name. */
    Response& clear_cookie(std::string_view name, std::string_view path = "/");

    // ---- Send helpers (buffered) ----

    /**
     * @brief Set the body and finish the response.
     * @param body         Payload (copied).
     * @param content_type Content-Type header value.
     * @return false when the response was already ended.
     */
    bool send(std::string_view body, std::string_view content_type = "text/plain; charset=utf-8");

    /** @brief send() with Content-Type text/html. */
    bool html(std::string_view html) { return send(html, "text/html; charset=utf-8"); }

    /** @brief Serialize @p j and finish the response as application/json. */
    bool json(const nlohmann::json& j);

    /**
     * @brief Set @p s and finish with a JSON body {"error": message}.
     * @return false when the response was already ended.
     */
    bool json_error(Status s, std::string_view message);

    /** @brief Finish with a status code and its reason phrase as plain text. */
    bool send_status(Status s);

    // ---- Incremental writing ----

    /**
     * @brief Append @p chunk to the body. Call end() when done.
     * @return false when the response was already ended.
     */
    bool write(std::string_view chunk);

    /** @brief Finalize the response (no more writes). */
    void end();

    /** @brief write() unless the response already ended. */
    bool tryWrite(std::string_view chunk) { return ended_ ? false : write(chunk); }

    // ---- Redirect ----

    /** @brief Finish with a redirect (302 by default) to @p url. */
    void redirect(std::string_view url, std::uint16_t code = 302);

    // ---- File responses ----

    /**
     * @brief Stream a file from disk as the response body.
     *
     * The server uses zero-copy sendfile(2) on plain sockets. Content-Type
     * is inferred from the file extension unless already set.
     *
     * @param fs_path       Path to the file.
     * @param download      When true, adds Content-Disposition: attachment.
     * @param download_name Filename presented to the browser (defaults to
     *                      the on-disk name).
     * @return false when the file cannot be opened (response not ended).
     */
    bool send_file(std::string_view fs_path, bool download = false,
                   std::string_view download_name = "");

    /**
     * @brief Stream a byte range of a file (used for HTTP Range support).
     * @return false when the file cannot be opened or the range is invalid.
     */
    bool send_file_range(std::string_view fs_path, std::uint64_t offset, std::uint64_t length);

    // ---- Introspection (used by the server; safe for middleware) ----

    /** @brief True after the response was finalized. */
    bool ended() const noexcept { return ended_; }
    /** @brief Current status code (0 = unset). */
    std::uint16_t status_code() const noexcept { return status_code_; }
    /** @brief Response headers. */
    const HeaderMap& headers() const noexcept { return headers_; }
    /** @brief Queued Set-Cookie values (one header each). */
    const std::vector<std::string>& set_cookies() const noexcept { return set_cookies_; }
    /** @brief Body as a view. */
    std::string_view body_view() const noexcept { return body_; }
    /** @brief Body as an owned string reference. */
    const std::string& body_string() const noexcept { return body_storage_; }
    /** @brief True when a body is present. */
    bool has_body() const noexcept { return !body_.empty() || !body_storage_.empty(); }

    /** @brief Response kind (Buffered / File / Stream). */
    Kind kind() const noexcept { return kind_; }
    /** @brief File path for Kind::File responses. */
    const std::string& file_path() const noexcept { return file_path_; }
    /** @brief File range start for Kind::File responses. */
    std::uint64_t file_offset() const noexcept { return file_offset_; }
    /** @brief File range length for Kind::File responses (0 = whole file). */
    std::uint64_t file_length() const noexcept { return file_length_; }

    // ---- Internal (server / SSE plumbing) ----

    /** @brief Internal: mark this response as a long-lived stream (SSE). */
    void mark_stream(std::shared_ptr<void> state) {
        kind_ = Kind::Stream;
        stream_state_ = std::move(state);
        ended_ = true;
    }
    /** @brief Internal: mark this response as a Pulse (WebSocket) upgrade. */
    void mark_pulse(std::shared_ptr<void> state) {
        kind_ = Kind::Pulse;
        stream_state_ = std::move(state);
        ended_ = true;
    }
    /** @brief Internal: stream/pulse state attached by mark_stream/mark_pulse. */
    const std::shared_ptr<void>& stream_state() const noexcept { return stream_state_; }

    /** @brief Internal: take the body, leaving the response empty. */
    std::string take_body() {
        ensure_body_owned_();
        std::string b = std::move(body_storage_);
        body_storage_.clear();
        body_ = {};
        return b;
    }

private:
    void ensure_body_owned_();

    std::uint16_t status_code_{0};
    HeaderMap     headers_{};
    std::vector<std::string> set_cookies_{};

    bool          ended_{false};
    Kind          kind_{Kind::Buffered};

    std::string   body_storage_{};
    std::string_view body_{};

    std::string   file_path_{};
    std::uint64_t file_offset_{0};
    std::uint64_t file_length_{0};

    std::shared_ptr<void> stream_state_{};
};

} // namespace socketify
