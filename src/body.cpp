/**
 * @file body.cpp
 * @brief JSON / urlencoded / multipart body parsers.
 */

#include "socketify/body.h"
#include "socketify/detail/utils.h"

namespace socketify::body {

bool is_json(const Request& req) {
    return detail::istarts_with(req.content_type(), "application/json");
}

bool is_form(const Request& req) {
    return detail::istarts_with(req.content_type(), "application/x-www-form-urlencoded");
}

bool is_multipart(const Request& req) {
    return detail::istarts_with(req.content_type(), "multipart/form-data");
}

std::optional<nlohmann::json> json(const Request& req) {
    auto b = req.body_view();
    if (b.empty()) return std::nullopt;
    auto j = nlohmann::json::parse(b, /*cb=*/nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) return std::nullopt;
    return j;
}

ParamMap form(const Request& req) {
    ParamMap out;
    detail::parse_query_string(req.body_view(), out);
    return out;
}

// ---------------------------------------------------------------------------
// multipart/form-data
// ---------------------------------------------------------------------------

namespace {

// Extract boundary="..." or boundary=... from a Content-Type value.
std::string extract_boundary_(std::string_view content_type) {
    constexpr std::string_view key = "boundary=";
    std::size_t pos = 0;
    while (pos < content_type.size()) {
        std::size_t found = content_type.find(key, pos);
        if (found == std::string_view::npos) return {};
        // Must be preceded by ';' or whitespace to be the actual parameter.
        if (found > 0) {
            char prev = content_type[found - 1];
            if (prev != ';' && prev != ' ' && prev != '\t') {
                pos = found + key.size();
                continue;
            }
        }
        std::string_view rest = content_type.substr(found + key.size());
        if (!rest.empty() && rest.front() == '"') {
            auto endq = rest.find('"', 1);
            if (endq == std::string_view::npos) return {};
            return std::string(rest.substr(1, endq - 1));
        }
        auto semi = rest.find(';');
        return std::string(detail::trim_view(
            semi == std::string_view::npos ? rest : rest.substr(0, semi)));
    }
    return {};
}

// Parse `name="..."` style parameters out of a Content-Disposition value.
std::string disposition_param_(std::string_view value, std::string_view param) {
    for (auto piece : detail::split_view(value, ';')) {
        piece = detail::trim_view(piece);
        auto eq = piece.find('=');
        if (eq == std::string_view::npos) continue;
        auto key = detail::trim_view(piece.substr(0, eq));
        if (!detail::iequal_ascii(key, param)) continue;
        auto val = detail::trim_view(piece.substr(eq + 1));
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
            val = val.substr(1, val.size() - 2);
        }
        return std::string(val);
    }
    return {};
}

} // namespace

std::optional<MultipartData> multipart(const Request& req) {
    if (!is_multipart(req)) return std::nullopt;

    std::string boundary = extract_boundary_(req.content_type());
    if (boundary.empty()) return std::nullopt;

    std::string_view bodyv = req.body_view();
    const std::string delim = "--" + boundary;

    // The body starts with the first delimiter (possibly after a CRLF).
    std::size_t pos = bodyv.find(delim);
    if (pos == std::string_view::npos) return std::nullopt;
    pos += delim.size();

    MultipartData out;

    while (true) {
        // After a delimiter: "--" means final boundary; else expect CRLF.
        if (pos + 2 <= bodyv.size() && bodyv.substr(pos, 2) == "--") break;
        if (pos + 2 > bodyv.size()) break;
        if (bodyv.substr(pos, 2) == "\r\n") pos += 2;

        // ---- Part headers ----
        std::string disposition;
        std::string content_type;
        while (true) {
            auto eol = bodyv.find("\r\n", pos);
            if (eol == std::string_view::npos) return std::nullopt;
            std::string_view line = bodyv.substr(pos, eol - pos);
            pos = eol + 2;
            if (line.empty()) break; // end of part headers

            auto colon = line.find(':');
            if (colon == std::string_view::npos) continue;
            auto key = detail::trim_view(line.substr(0, colon));
            auto val = detail::trim_view(line.substr(colon + 1));
            if (detail::iequal_ascii(key, "Content-Disposition")) {
                disposition.assign(val);
            } else if (detail::iequal_ascii(key, "Content-Type")) {
                content_type.assign(val);
            }
        }

        // ---- Part body (up to CRLF + delimiter) ----
        const std::string closing = "\r\n" + delim;
        auto end = bodyv.find(closing, pos);
        if (end == std::string_view::npos) return std::nullopt;
        std::string_view data = bodyv.substr(pos, end - pos);
        pos = end + closing.size();

        std::string name = disposition_param_(disposition, "name");
        std::string filename = disposition_param_(disposition, "filename");

        if (!filename.empty() || !content_type.empty()) {
            FilePart fp;
            fp.name = std::move(name);
            fp.filename = std::move(filename);
            fp.content_type = std::move(content_type);
            fp.data.assign(data.data(), data.size());
            out.files.push_back(std::move(fp));
        } else if (!name.empty()) {
            out.fields[std::move(name)] = std::string(data);
        }
    }

    return out;
}

} // namespace socketify::body
