/**
 * @file static_files.cpp
 * @brief Static file middleware: safe path resolution, conditional requests,
 *        range requests and zero-copy streaming via Response::send_file.
 */

#include "socketify/static_files.h"
#include "socketify/detail/utils.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
namespace socketify::static_files {

// ---------- small helpers ----------

static inline bool contains_dot_segment(const std::vector<std::string_view>& parts,
                                        bool allow_hidden) {
    for (auto& p : parts) {
        if (p == "." || p == "..") return true;
        if (!allow_hidden && !p.empty() && p[0] == '.') return true; // dotfile
    }
    return false;
}

static inline std::time_t to_time_t(const fs::file_time_type& ftime) {
    using namespace std::chrono;
    auto sctp = time_point_cast<system_clock::duration>(ftime - fs::file_time_type::clock::now()
                     + system_clock::now());
    return system_clock::to_time_t(sctp);
}

static std::string normalize_mount(std::string m) {
    if (m.empty()) return "/";
    if (m[0] != '/') m.insert(m.begin(), '/');
    if (m.size() > 1 && m.back() == '/') m.pop_back();
    return m;
}

static std::string safe_join(const fs::path& root, std::string_view url_subpath,
                             bool allow_hidden, bool& ok) {
    ok = false;
    auto parts = detail::split_view(url_subpath, '/');
    if (contains_dot_segment(parts, allow_hidden)) {
        return {};
    }
    fs::path joined = root;
    for (auto& p : parts) joined /= fs::path(std::string(p));
    std::error_code ec;
    auto norm = fs::weakly_canonical(joined, ec);
    if (ec) return {};
    auto root_norm = fs::weakly_canonical(root, ec);
    if (ec) return {};
    auto nstr = norm.string();
    auto rstr = root_norm.string();
    if (!detail::starts_with(nstr, rstr)) return {};
    // Reject "/rootX" passing a "/root" prefix check.
    if (nstr.size() > rstr.size() && rstr.back() != '/' && nstr[rstr.size()] != '/') return {};
    ok = true;
    return nstr;
}

static std::string weak_etag(std::uintmax_t size, std::time_t mtime) {
    return "W/\"" + std::to_string(size) + "-" + std::to_string(static_cast<long long>(mtime)) + "\"";
}

static bool parse_single_range(std::string_view hval, std::uintmax_t size,
                               std::uintmax_t& start, std::uintmax_t& end) {
    std::string v(hval);
    auto pos = v.find('=');
    if (pos == std::string::npos) return false;
    auto unit = detail::to_lower_copy(v.substr(0, pos));
    if (unit != "bytes") return false;
    auto spec = v.substr(pos + 1);
    if (spec.find(',') != std::string::npos) return false;

    std::string_view specv = detail::trim_view(spec);
    spec.assign(specv);

    std::uintmax_t s = 0, e = 0;
    auto dash = spec.find('-');
    if (dash == std::string::npos) return false;

    std::string a = spec.substr(0, dash);
    std::string b = spec.substr(dash + 1);

    if (a.empty()) {
        // suffix range: last N bytes
        char* endp = nullptr;
        long long n = std::strtoll(b.c_str(), &endp, 10);
        if (endp == b.c_str() || n <= 0) return false;
        if (static_cast<std::uintmax_t>(n) >= size) { s = 0; e = size ? size - 1 : 0; }
        else { s = size - static_cast<std::uintmax_t>(n); e = size - 1; }
    } else {
        char* endp = nullptr;
        long long aa = std::strtoll(a.c_str(), &endp, 10);
        if (endp == a.c_str() || aa < 0) return false;
        s = static_cast<std::uintmax_t>(aa);
        if (b.empty()) {
            e = size ? size - 1 : 0;
        } else {
            long long bb = std::strtoll(b.c_str(), &endp, 10);
            if (endp == b.c_str() || bb < 0) return false;
            e = static_cast<std::uintmax_t>(bb);
        }
        if (s >= size || e < s) return false;
        if (e >= size) e = size - 1;
    }

    start = s; end = e;
    return true;
}

static std::string html_escape_(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

static std::string list_directory_html(const fs::path& dir, std::string_view url_path) {
    std::ostringstream oss;
    oss << "<!doctype html><html><head><meta charset=\"utf-8\"><title>Index of "
        << html_escape_(url_path) << "</title></head><body><h1>Index of "
        << html_escape_(url_path) << "</h1><ul>";
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        auto name = entry.path().filename().string();
        auto esc = html_escape_(name);
        oss << "<li><a href=\"" << esc;
        if (entry.is_directory()) oss << "/";
        oss << "\">" << esc << (entry.is_directory() ? "/" : "") << "</a></li>";
    }
    oss << "</ul></body></html>";
    return oss.str();
}

// ---------- main middleware ----------

Middleware serve(Options opts) {
    opts.mount = normalize_mount(std::move(opts.mount));

    std::error_code ec;
    fs::path root = fs::absolute(opts.root, ec);
    if (ec) root = fs::path(opts.root);

    return [opts, root](Request& req, Response& res, Next next) {
        if (!(req.method() == Method::GET || req.method() == Method::HEAD)) {
            if (opts.fallthrough) { next(); return; }
            res.status(Status::MethodNotAllowed)
               .set_header("Allow", "GET, HEAD")
               .send("Method Not Allowed\n");
            return;
        }

        std::string path(req.path());
        if (path.empty()) path = "/";

        if (!detail::starts_with(path, opts.mount)) {
            next();
            return;
        }

        std::string sub = path.substr(opts.mount.size());
        if (!sub.empty() && sub.front() == '/') sub.erase(sub.begin());

        bool ok = false;
        std::string fullpath = safe_join(root, sub, opts.allow_hidden, ok);
        if (!ok) {
            if (opts.fallthrough) { next(); return; }
            res.status(Status::NotFound).send("Not Found\n");
            return;
        }

        std::error_code fec;
        fs::file_status st = fs::status(fullpath, fec);
        if (fec || !fs::exists(st)) {
            if (opts.fallthrough) { next(); return; }
            res.status(Status::NotFound).send("Not Found\n");
            return;
        }

        if (fs::is_directory(st)) {
            if (opts.auto_index) {
                for (const auto& name : opts.index_names) {
                    fs::path candidate = fs::path(fullpath) / name;
                    if (fs::exists(candidate, fec) && !fec && fs::is_regular_file(candidate, fec)) {
                        fullpath = candidate.string();
                        st = fs::status(candidate, fec);
                        break;
                    }
                }
            }
            if (fs::is_directory(st)) {
                if (!opts.directory_listing) {
                    if (opts.fallthrough) { next(); return; }
                    res.status(Status::NotFound).send("Not Found\n");
                    return;
                }
                auto html = list_directory_html(fullpath, path);
                if (opts.cache_max_age > 0) {
                    std::string cc = "public, max-age=" + std::to_string(opts.cache_max_age);
                    if (opts.immutable) cc += ", immutable";
                    res.set_header(H_CacheControl, cc);
                }
                res.html(html);
                return;
            }
        }

        auto fsize = fs::file_size(fullpath, fec);
        if (fec) { if (opts.fallthrough) { next(); return; } res.status(Status::NotFound).send("Not Found\n"); return; }

        auto ftime = fs::last_write_time(fullpath, fec);
        if (fec) { ftime = fs::file_time_type::clock::now(); }
        std::time_t mtime = to_time_t(ftime);

        res.set_header(H_ContentType, content_type_for_path(fullpath));

        if (opts.cache_max_age > 0) {
            std::string cc = "public, max-age=" + std::to_string(opts.cache_max_age);
            if (opts.immutable) cc += ", immutable";
            res.set_header(H_CacheControl, cc);
        }
        if (opts.last_modified) {
            res.set_header(H_LastModified, detail::http_date(static_cast<std::int64_t>(mtime)));
        }
        std::string etag;
        if (opts.etag) {
            etag = weak_etag(fsize, mtime);
            res.set_header(H_ETag, etag);
        }

        // ---- Conditional requests ----
        if (opts.etag) {
            auto inm = req.header("If-None-Match");
            if (!inm.empty() && inm == etag) {
                res.status(Status::NotModified).end();
                return;
            }
        }
        if (opts.last_modified) {
            auto ims = req.header("If-Modified-Since");
            if (!ims.empty()) {
                if (auto since = detail::parse_http_date(ims)) {
                    if (static_cast<std::int64_t>(mtime) <= *since) {
                        res.status(Status::NotModified).end();
                        return;
                    }
                }
            }
        }

        res.set_header("Accept-Ranges", "bytes");

        // ---- Range requests ----
        std::uintmax_t start = 0;
        std::uintmax_t end   = fsize ? fsize - 1 : 0;
        bool ranged = false;

        auto rng = req.header(H_Range);
        if (!rng.empty()) {
            std::uintmax_t rs = 0, re = 0;
            if (parse_single_range(rng, fsize, rs, re)) {
                start = rs; end = re;
                ranged = true;
            } else {
                res.status(Status::RangeNotSatisfiable)
                   .set_header(H_ContentRange, "bytes */" + std::to_string(fsize))
                   .end();
                return;
            }
        }

        std::uintmax_t content_len = (fsize == 0) ? 0 : (end - start + 1);

        if (ranged) {
            res.status(Status::PartialContent);
            res.set_header(H_ContentRange, "bytes " + std::to_string(start) + "-" +
                                               std::to_string(end) + "/" + std::to_string(fsize));
        }

        // Stream from disk (sendfile on plain sockets). HEAD responses send
        // headers only; the server strips the body automatically.
        if (!res.send_file_range(fullpath, start, content_len)) {
            if (opts.fallthrough) { next(); return; }
            res.status(Status::InternalServerError).send("Failed to read file\n");
        }
    };
}

} // namespace socketify::static_files
