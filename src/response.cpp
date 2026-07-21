/**
 * @file response.cpp
 * @brief Response builder implementation.
 */

#include "socketify/response.h"
#include "socketify/detail/file_io.h"

#include <filesystem>

namespace socketify {

Response& Response::set_header(std::string_view key, std::string_view value) {
    headers_[std::string(key)] = std::string(value);
    return *this;
}

Response& Response::set_cookie(std::string_view cookie_line) {
    set_cookies_.emplace_back(cookie_line);
    return *this;
}

Response& Response::clear_cookie(std::string_view name, std::string_view path) {
    return set_cookie(cookies::expired(name, path));
}

bool Response::send(std::string_view body, std::string_view content_type) {
    if (ended_) return false;
    // Do not clobber a Content-Type set explicitly by the handler.
    if (headers_.find(std::string(H_ContentType)) == headers_.end()) {
        set_content_type(content_type);
    }
    body_storage_.assign(body.data(), body.size());
    body_ = body_storage_;
    if (status_code_ == 0) status_code_ = 200;
    ended_ = true;
    return true;
}

bool Response::json(const nlohmann::json& j) {
    if (ended_) return false;
    std::string dump = j.dump();
    set_content_type("application/json; charset=utf-8");
    body_storage_.swap(dump);
    body_ = body_storage_;
    if (status_code_ == 0) status_code_ = 200;
    ended_ = true;
    return true;
}

bool Response::json_error(Status s, std::string_view message) {
    if (ended_) return false;
    status(s);
    return json(nlohmann::json{{"error", std::string(message)}});
}

bool Response::send_status(Status s) {
    if (ended_) return false;
    status(s);
    std::string body{reason(s)};
    body.push_back('\n');
    return send(body);
}

bool Response::write(std::string_view chunk) {
    if (ended_) return false;
    if (!chunk.empty()) {
        body_storage_.append(chunk.data(), chunk.size());
        body_ = body_storage_;
    }
    return true;
}

void Response::end() {
    if (ended_) return;
    if (status_code_ == 0) status_code_ = 200;
    ended_ = true;
}

void Response::redirect(std::string_view url, std::uint16_t code) {
    if (ended_) return;
    status(code);
    set_header(H_Location, url);
    const char tpl_prefix[] = "<html><head><title>Redirect</title></head><body>Redirecting to ";
    const char tpl_suffix[] = "</body></html>";
    body_storage_.clear();
    body_storage_.reserve(sizeof(tpl_prefix) - 1 + url.size() + sizeof(tpl_suffix) - 1);
    body_storage_.append(tpl_prefix, sizeof(tpl_prefix) - 1);
    body_storage_.append(url.data(), url.size());
    body_storage_.append(tpl_suffix, sizeof(tpl_suffix) - 1);
    body_ = body_storage_;
    set_content_type("text/html; charset=utf-8");
    ended_ = true;
}

bool Response::send_file(std::string_view fs_path, bool download, std::string_view download_name) {
    if (ended_) return false;

    detail::FileHandle fh;
    if (!fh.open(fs_path)) return false;

    kind_ = Kind::File;
    file_path_.assign(fs_path);
    file_offset_ = 0;
    file_length_ = fh.size();

    if (headers_.find(std::string(H_ContentType)) == headers_.end()) {
        set_content_type(content_type_for_path(fs_path));
    }
    if (download) {
        std::string name(download_name);
        if (name.empty()) {
            name = std::filesystem::path(std::string(fs_path)).filename().string();
        }
        set_header("Content-Disposition", "attachment; filename=\"" + name + "\"");
    }
    ended_ = true;
    return true;
}

bool Response::send_file_range(std::string_view fs_path, std::uint64_t offset, std::uint64_t length) {
    if (ended_) return false;

    detail::FileHandle fh;
    if (!fh.open(fs_path)) return false;
    if (offset > fh.size() || offset + length > fh.size()) return false;

    kind_ = Kind::File;
    file_path_.assign(fs_path);
    file_offset_ = offset;
    file_length_ = length;

    if (headers_.find(std::string(H_ContentType)) == headers_.end()) {
        set_content_type(content_type_for_path(fs_path));
    }
    ended_ = true;
    return true;
}

void Response::ensure_body_owned_() {
    if (body_.empty()) return;
    if (body_.data() >= body_storage_.data() &&
        body_.data() < body_storage_.data() + body_storage_.size()) {
        return;
    }
    body_storage_.assign(body_.data(), body_.size());
    body_ = body_storage_;
}

} // namespace socketify
