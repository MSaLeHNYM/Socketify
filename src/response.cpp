#include "socketify/response.h"
#include "socketify/http.h"

#include <algorithm>
#include <cstdio>

namespace socketify {

/**
 * @brief Sets a header field.
 * @param key The header key.
 * @param value The header value.
 * @return A reference to the Response object.
 */
Response& Response::set_header(std::string_view key, std::string_view value) {
    headers_[std::string(key)] = std::string(value);
    return *this;
}

/**
 * @brief Sets a cookie.
 * @param cookie_line The full cookie string.
 * @return A reference to the Response object.
 */
Response& Response::set_cookie(std::string_view cookie_line) {
    // Multiple Set-Cookie headers are allowed; store as comma-joined fallback for now.
    // (Later we can keep a vector or multi-map; most servers send separate header lines.)
    auto it = headers_.find(std::string(H_SetCookie));
    if (it == headers_.end()) {
        headers_.emplace(std::string(H_SetCookie), std::string(cookie_line));
    } else {
        it->second.append(", ").append(cookie_line);
    }
    return *this;
}

/**
 * @brief Sends a response with a body.
 * @param body The response body.
 * @param content_type The content type of the body.
 * @return true if the response was sent successfully, false otherwise.
 */
bool Response::send(std::string_view body, std::string_view content_type) {
    if (ended_) return false;
    set_content_type(content_type);

    // Own the body (for now) and set content-length
    body_storage_.assign(body.data(), body.size());
    body_ = body_storage_;
    set_header(H_ContentLength, std::to_string(body_.size()));

    ended_ = true;
    return true;
}

/**
 * @brief Sends a JSON response.
 * @param j The JSON object.
 * @return true if the response was sent successfully, false otherwise.
 */
bool Response::json(const nlohmann::json& j) {
    if (ended_) return false;
    std::string dump = j.dump(); // default: no pretty-print, UTF-8
    set_content_type("application/json; charset=utf-8");
    body_storage_.swap(dump);
    body_ = body_storage_;
    set_header(H_ContentLength, std::to_string(body_.size()));
    ended_ = true;
    return true;
}

/**
 * @brief Writes a chunk of data to the response body.
 * @param chunk The data to write.
 * @return true if the write was successful, false otherwise.
 */
bool Response::write(std::string_view chunk) {
    if (ended_) return false;
    // Accumulate into owned buffer (we'll set Content-Length in end()).
    if (!chunk.empty()) {
        body_storage_.append(chunk.data(), chunk.size());
        body_ = body_storage_;
    }
    return true;
}

/**
 * @brief Finalizes the response.
 */
void Response::end() {
    if (ended_) return;
    set_header(H_ContentLength, std::to_string(body_.size()));
    ended_ = true;
}

/**
 * @brief Redirects the client to a different URL.
 * @param url The URL to redirect to.
 * @param code The HTTP status code for the redirection.
 */
void Response::redirect(std::string_view url, std::uint16_t code) {
    if (ended_) return;
    status(code);
    set_header(H_Location, url);
    // Minimal body for browsers; not required but nice to have
    const char tpl_prefix[] = "<html><head><title>Redirect</title></head><body>Redirecting to ";
    const char tpl_suffix[] = "</body></html>";
    body_storage_.reserve(sizeof(tpl_prefix) - 1 + url.size() + sizeof(tpl_suffix) - 1);
    body_storage_.append(tpl_prefix, sizeof(tpl_prefix) - 1);
    body_storage_.append(url.data(), url.size());
    body_storage_.append(tpl_suffix, sizeof(tpl_suffix) - 1);
    body_ = body_storage_;
    set_content_type("text/html; charset=utf-8");
    set_header(H_ContentLength, std::to_string(body_.size()));
    ended_ = true;
}

/**
 * @brief Sends a file as the response.
 * @param fs_path The path to the file.
 * @param download Whether to force a download.
 * @param download_name The name to use for the downloaded file.
 * @return true if the file was sent successfully, false otherwise.
 */
bool Response::send_file(std::string_view /*fs_path*/, bool /*download*/, std::string_view /*download_name*/) {
    // TODO(socketify): wire to detail::file_io with sendfile/streaming.
    // Returning false communicates "not implemented" to caller for now.
    return false;
}

void Response::ensure_body_owned_() {
    if (body_.data() >= body_storage_.data() &&
        body_.data() <  body_storage_.data() + body_storage_.size()) {
        // already owned
        return;
    }
    // Copy view into owned storage
    body_storage_.assign(body_.data(), body_.size());
    body_ = body_storage_;
}

} // namespace socketify