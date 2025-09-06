#include "response.h"
#include <iostream>

namespace socketify {

Response& Response::status_code(int code) {
    status_ = code;
    return *this;
}

Response& Response::set_header(std::string_view k, std::string_view v) {
    headers_[std::string(k)] = std::string(v);
    return *this;
}

Response& Response::set_cookie(std::string_view name,
                               std::string_view val,
                               std::chrono::seconds max_age,
                               bool http_only,
                               bool secure,
                               std::string_view same_site,
                               std::optional<std::string_view> path,
                               std::optional<std::string_view> domain) {
    std::ostringstream oss;
    oss << name << "=" << val;
    if (max_age.count() > 0) oss << "; Max-Age=" << max_age.count();
    if (path)   oss << "; Path=" << *path;
    if (domain) oss << "; Domain=" << *domain;
    if (secure) oss << "; Secure";
    if (http_only) oss << "; HttpOnly";
    oss << "; SameSite=" << same_site;
    headers_["Set-Cookie"] = oss.str();
    return *this;
}

void Response::text(std::string_view s, int code) {
    status_code(code);
    set_header("Content-Type", "text/plain; charset=utf-8");
    committed_ = true;
    std::cout << "[Response " << status_ << "] " << s << "\n";
}

void Response::bytes(const void* data, std::size_t n, int code) {
    status_code(code);
    set_header("Content-Type", "application/octet-stream");
    committed_ = true;
    std::cout << "[Response " << status_ << "] " << n << " bytes sent\n";
}

void Response::file(const std::filesystem::path& p,
                    std::optional<std::string_view> download_as) {
    status_code(200);
    set_header("Content-Type", "application/octet-stream");
    if (download_as) {
        set_header("Content-Disposition",
                   std::string("attachment; filename=") + std::string(*download_as));
    }
    committed_ = true;
    std::cout << "[Response File] " << p << "\n";
}

void Response::redirect(std::string_view location, int code) {
    status_code(code);
    set_header("Location", location);
    committed_ = true;
    std::cout << "[Redirect] " << location << "\n";
}

void Response::write(std::string_view chunk) {
    committed_ = true;
    std::cout << "[Chunk] " << chunk << "\n";
}

void Response::end() {
    std::cout << "[Stream ended]\n";
}

} // namespace socketify
