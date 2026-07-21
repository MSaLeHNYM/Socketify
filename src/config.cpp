/**
 * @file config.cpp
 * @brief Environment + .env configuration loading.
 */

#include "socketify/config.h"
#include "socketify/detail/utils.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

extern char** environ;

namespace socketify::config {

namespace {

std::string strip(std::string_view s) { return std::string(detail::trim_view(s)); }

// Remove one layer of matching single/double quotes.
std::string unquote(std::string v) {
    if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front()) {
        v = v.substr(1, v.size() - 2);
    }
    return v;
}

} // namespace

std::map<std::string, std::string> Config::parse_env(std::string_view text) {
    std::map<std::string, std::string> out;
    std::string line;
    std::istringstream stream{std::string(text)};
    while (std::getline(stream, line)) {
        // Trim trailing CR (CRLF files) and surrounding whitespace.
        std::string trimmed = strip(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        if (detail::istarts_with(trimmed, "export ")) {
            trimmed = strip(trimmed.substr(7));
        }

        auto eq = trimmed.find('=');
        if (eq == std::string::npos) continue;

        std::string key = strip(trimmed.substr(0, eq));
        std::string value = strip(trimmed.substr(eq + 1));
        if (key.empty()) continue;

        // Strip an inline comment only for unquoted values.
        if (!value.empty() && value.front() != '"' && value.front() != '\'') {
            auto hash = value.find(" #");
            if (hash != std::string::npos) value = strip(value.substr(0, hash));
        }
        out[key] = unquote(std::move(value));
    }
    return out;
}

Config Config::from_env() {
    Config c;
    for (char** e = environ; e && *e; ++e) {
        std::string_view entry(*e);
        auto eq = entry.find('=');
        if (eq == std::string_view::npos) continue;
        c.values_[std::string(entry.substr(0, eq))] = std::string(entry.substr(eq + 1));
    }
    return c;
}

Config Config::from_file(const std::string& path) {
    Config c;
    c.merge_file(path, /*overrides=*/true);
    return c;
}

Config& Config::merge_file(const std::string& path, bool overrides) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return *this;
    std::ostringstream buf;
    buf << in.rdbuf();
    for (auto& [k, v] : parse_env(buf.str())) {
        if (overrides || !values_.count(k)) values_[k] = v;
    }
    return *this;
}

Config& Config::set(std::string key, std::string value) {
    values_[std::move(key)] = std::move(value);
    return *this;
}

std::optional<std::string> Config::get(const std::string& key) const {
    auto it = values_.find(key);
    if (it == values_.end()) return std::nullopt;
    return it->second;
}

std::string Config::get_or(const std::string& key, std::string fallback) const {
    auto v = get(key);
    return v ? *v : std::move(fallback);
}

std::optional<long long> Config::get_int(const std::string& key) const {
    auto v = get(key);
    if (!v) return std::nullopt;
    try {
        std::size_t pos = 0;
        long long n = std::stoll(*v, &pos);
        if (pos != v->size()) return std::nullopt;
        return n;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<double> Config::get_double(const std::string& key) const {
    auto v = get(key);
    if (!v) return std::nullopt;
    try {
        std::size_t pos = 0;
        double n = std::stod(*v, &pos);
        if (pos != v->size()) return std::nullopt;
        return n;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<bool> Config::get_bool(const std::string& key) const {
    auto v = get(key);
    if (!v) return std::nullopt;
    std::string s = detail::to_lower_copy(*v);
    if (s == "1" || s == "true" || s == "yes" || s == "on") return true;
    if (s == "0" || s == "false" || s == "no" || s == "off" || s.empty()) return false;
    return std::nullopt;
}

std::string Config::require(const std::string& key) const {
    auto v = get(key);
    if (!v) throw Error(key);
    return *v;
}

long long Config::require_int(const std::string& key) const {
    auto v = get_int(key);
    if (!v) throw Error(key);
    return *v;
}

} // namespace socketify::config
