#include "socketify/static_files.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <iomanip>   // get_time
#include <cctype>    // std::isspace

namespace fs = std::filesystem;
namespace socketify::static_files {

// ---------- small helpers ----------

static inline char ascii_lower(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc >= 'A' && uc <= 'Z') return static_cast<char>(uc - 'A' + 'a');
    return static_cast<char>(uc);
}
static inline std::string to_lower(std::string s) { for (auto& c : s) c = ascii_lower(c); return s; }

static inline bool starts_with(std::string_view s, std::string_view pfx) {
    return s.size() >= pfx.size() && std::equal(pfx.begin(), pfx.end(), s.begin());
}

static inline bool contains_dot_segment(const std::vector<std::string>& parts) {
    for (auto& p : parts) {
        if (p == "." || p == "..") return true;
        if (!p.empty() && p[0] == '.') return true; // dotfile
    }
    return false;
}

static inline std::string http_date(std::time_t t) {
    std::tm gmt{};
#if defined(_WIN32)
    gmtime_s(&gmt, &t);
#else
    gmtime_r(&t, &gmt);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &gmt);
    return std::string(buf);
}

static inline std::time_t to_time_t(const fs::file_time_type& ftime) {
    using namespace std::chrono;
    auto sctp = time_point_cast<system_clock::duration>(ftime - fs::file_time_type::clock::now()
                     + system_clock::now());
    return system_clock::to_time_t(sctp);
}

// Very small content-type map
static std::string guess_content_type(const std::string& path) {
    auto ext = to_lower(fs::path(path).extension().string());
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js") return "application/javascript; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif") return "image/gif";
    if (ext == ".webp") return "image/webp";
    if (ext == ".txt") return "text/plain; charset=utf-8";
    if (ext == ".xml") return "application/xml; charset=utf-8";
    if (ext == ".pdf") return "application/pdf";
    // default
    return "application/octet-stream";
}

static inline std::vector<std::string> split_path(std::string_view s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start < s.size()) {
        size_t pos = s.find('/', start);
        if (pos == std::string_view::npos) {
            if (start < s.size()) out.emplace_back(std::string(s.substr(start)));
            break;
        } else {
            if (pos > start) out.emplace_back(std::string(s.substr(start, pos - start)));
            start = pos + 1;
        }
    }
    return out;
}

static std::string normalize_mount(std::string m) {
    if (m.empty()) return "/";
    if (m[0] != '/') m.insert(m.begin(), '/');
    if (m.size() > 1 && m.back() == '/') m.pop_back();
    return m;
}

static std::string safe_join(const fs::path& root, std::string_view url_subpath, bool allow_hidden, bool& ok) {
    ok = false;
    auto parts = split_path(url_subpath);
    if (!allow_hidden && contains_dot_segment(parts)) {
        return {};
    }
    fs::path joined = root;
    for (auto& p : parts) joined /= p;
    std::error_code ec;
    auto norm = fs::weakly_canonical(joined, ec);
    if (ec) {
        fs::path lex = (root / fs::path(url_subpath)).lexically_normal();
        auto lex_abs = fs::absolute(lex, ec);
        if (ec) return {};
        auto root_abs = fs::absolute(root, ec);
        if (ec) return {};
        auto lex_str  = lex_abs.string();
        auto root_str = root_abs.string();
        if (!starts_with(lex_str, root_str)) return {};
        ok = true;
        return lex_abs.string();
    }
    auto root_norm = fs::weakly_canonical(root, ec);
    if (ec) return {};
    auto nstr = norm.string();
    auto rstr = root_norm.string();
    if (!starts_with(nstr, rstr)) return {};
    ok = true;
    return nstr;
}

static std::string weak_etag(std::uintmax_t size, std::time_t mtime) {
    return "W/\"" + std::to_string(size) + "-" + std::to_string(static_cast<long long>(mtime)) + "\"";
}

static bool parse_http_date(std::string_view s, std::time_t& out) {
    // IMF-fixdate, e.g., "Wed, 21 Oct 2015 07:28:00 GMT"
    std::tm tm{};
    std::istringstream iss{std::string(s)};   // <- brace-init fixes most-vexing-parse
    iss.imbue(std::locale::classic());
    iss >> std::get_time(&tm, "%a, %d %b %Y %H:%M:%S GMT");
    if (iss.fail()) return false;
#if defined(_WIN32)
    out = _mkgmtime(&tm);
#else
    out = timegm(&tm);
#endif
    return true;
}

static bool parse_single_range(std::string_view hval, std::uintmax_t size,
                               std::uintmax_t& start, std::uintmax_t& end) {
    std::string v(hval);
    auto pos = v.find('=');
    if (pos == std::string::npos) return false;
    auto unit = to_lower(v.substr(0, pos));
    if (unit != "bytes") return false;
    auto spec = v.substr(pos + 1);
    if (spec.find(',') != std::string::npos) return false;

    auto trim = [](std::string& s){
        auto issp = [](unsigned char c){ return std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](char c){ return !issp(c); }));
        s.erase(std::find_if(s.rbegin(), s.rend(), [&](char c){ return !issp(c); }).base(), s.end());
    };
    trim(spec);

    std::uintmax_t s = 0, e = 0;
    auto dash = spec.find('-');
    if (dash == std::string::npos) return false;

    std::string a = spec.substr(0, dash);
    std::string b = spec.substr(dash + 1);

    if (a.empty()) {
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

static bool read_file_range(const std::string& path, std::uintmax_t start, std::uintmax_t len, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(static_cast<std::streamoff>(start));
    out.resize(static_cast<size_t>(len));
    f.read(reinterpret_cast<char*>(&out[0]), static_cast<std::streamsize>(len));
    return f.good() || f.eof();
}

static std::string list_directory_html(const fs::path& dir, std::string_view url_path) {
    std::ostringstream oss;
    oss << "<!doctype html><html><head><meta charset=\"utf-8\"><title>Index of "
        << url_path << "</title></head><body><h1>Index of " << url_path << "</h1><ul>";
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        auto name = entry.path().filename().string();
        oss << "<li><a href=\"" << name;
        if (entry.is_directory()) oss << "/";
        oss << "\">" << name << (entry.is_directory() ? "/" : "") << "</a></li>";
    }
    oss << "</ul></body></html>";
    return oss.str();
}

// ---------- main middleware ----------

/**
 * @brief Creates a new middleware for serving static files.
 * @param opts The options for serving static files.
 * @return A middleware function.
 */
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

        std::string path(req.path()); // <-- construct std::string from string_view
        if (path.empty()) path = "/";

        if (!starts_with(path, opts.mount)) {
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
                res.set_header(H_ContentType, "text/html; charset=utf-8");
                if (opts.cache_max_age > 0) {
                    std::string cc = "public, max-age=" + std::to_string(opts.cache_max_age);
                    if (opts.immutable) cc += ", immutable";
                    res.set_header("Cache-Control", cc);
                }
                res.send(std::move(html));
                return;
            }
        }

        auto fsize = fs::file_size(fullpath, fec);
        if (fec) { if (opts.fallthrough) { next(); return; } res.status(Status::NotFound).send("Not Found\n"); return; }

        auto ftime = fs::last_write_time(fullpath, fec);
        if (fec) { ftime = fs::file_time_type::clock::now(); }
        std::time_t mtime = to_time_t(ftime);

        res.set_header(H_ContentType, guess_content_type(fullpath));

        if (opts.cache_max_age > 0) {
            std::string cc = "public, max-age=" + std::to_string(opts.cache_max_age);
            if (opts.immutable) cc += ", immutable";
            res.set_header("Cache-Control", cc);
        }
        if (opts.last_modified) {
            res.set_header("Last-Modified", http_date(mtime));
        }
        std::string etag;
        if (opts.etag) {
            etag = weak_etag(fsize, mtime);
            res.set_header("ETag", etag);
        }

        auto inm_it = req.headers().find("If-None-Match");
        if (opts.etag && inm_it != req.headers().end()) {
            if (inm_it->second == etag) {
                res.status(Status::NotModified);
                res.set_header(H_ContentLength, "0");
                res.end();
                return;
            }
        }
        auto ims_it = req.headers().find("If-Modified-Since");
        if (opts.last_modified && ims_it != req.headers().end()) {
            std::time_t since{};
            if (parse_http_date(ims_it->second, since)) {
                if (mtime <= since) {
                    res.status(Status::NotModified);
                    res.set_header(H_ContentLength, "0");
                    res.end();
                    return;
                }
            }
        }

        std::uintmax_t start = 0;
        std::uintmax_t end   = fsize ? fsize - 1 : 0;
        bool ranged = false;

        auto rng_it = req.headers().find("Range");
        if (rng_it != req.headers().end()) {
            std::uintmax_t rs = 0, re = 0;
            if (parse_single_range(rng_it->second, fsize, rs, re)) {
                start = rs; end = re;
                ranged = true;
            } else {
                std::string cr = "bytes */" + std::to_string(fsize);
                res.status(Status::RangeNotSatisfiable)
                   .set_header("Content-Range", cr)
                   .set_header(H_ContentLength, "0")
                   .end();
                return;
            }
        }

        std::uintmax_t content_len = (end >= start) ? (end - start + 1) : 0;

        if (ranged) {
            res.status(Status::PartialContent);
            std::string cr = "bytes " + std::to_string(start) + "-" + std::to_string(end) + "/" + std::to_string(fsize);
            res.set_header("Content-Range", cr);
        }

        if (req.method() == Method::HEAD) {
            res.set_header(H_ContentLength, std::to_string(content_len));
            res.end();
            return;
        }

        std::string data;
        if (content_len > 0) {
            if (!read_file_range(fullpath, start, content_len, data)) {
                if (opts.fallthrough) { next(); return; }
                res.status(Status::InternalServerError).send("Failed to read file\n");
                return;
            }
        }

        res.set_header(H_ContentLength, std::to_string(data.size()));
        res.send(std::move(data));
    };
}

} // namespace socketify::static_files