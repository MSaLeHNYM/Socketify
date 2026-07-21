/**
 * @file validate.cpp
 * @brief JSON body validation engine.
 */

#include "socketify/validate.h"

#include <regex>

namespace socketify::validate {

namespace {

const char* type_name(Type t) {
    switch (t) {
        case Type::String: return "a string";
        case Type::Integer: return "an integer";
        case Type::Number: return "a number";
        case Type::Boolean: return "a boolean";
        case Type::Array: return "an array";
        case Type::Object: return "an object";
        case Type::Any: break;
    }
    return "valid";
}

bool type_matches(Type t, const nlohmann::json& v) {
    switch (t) {
        case Type::Any: return true;
        case Type::String: return v.is_string();
        case Type::Integer: return v.is_number_integer();
        case Type::Number: return v.is_number();
        case Type::Boolean: return v.is_boolean();
        case Type::Array: return v.is_array();
        case Type::Object: return v.is_object();
    }
    return true;
}

bool looks_like_email(const std::string& s) {
    auto at = s.find('@');
    if (at == std::string::npos || at == 0) return false;
    auto dot = s.find('.', at);
    return dot != std::string::npos && dot + 1 < s.size();
}

// Size metric used by min/max: string length, array/object size, or numeric value.
double magnitude(const nlohmann::json& v, bool& is_count) {
    if (v.is_string()) { is_count = true; return static_cast<double>(v.get_ref<const std::string&>().size()); }
    if (v.is_array() || v.is_object()) { is_count = true; return static_cast<double>(v.size()); }
    is_count = false;
    return v.is_number() ? v.get<double>() : 0.0;
}

} // namespace

void Field::check(const nlohmann::json& doc, std::vector<Error>& out) const {
    const bool present = doc.is_object() && doc.contains(name_) && !doc.at(name_).is_null();

    if (!present) {
        if (required_) out.push_back({name_, "is required"});
        return;
    }

    const nlohmann::json& v = doc.at(name_);

    if (type_ != Type::Any && !type_matches(type_, v)) {
        out.push_back({name_, std::string("must be ") + type_name(type_)});
        return; // further checks assume the type held
    }

    if (email_ && (!v.is_string() || !looks_like_email(v.get<std::string>()))) {
        out.push_back({name_, "must be a valid email"});
    }

    if (min_ || max_) {
        bool is_count = false;
        double m = magnitude(v, is_count);
        if (min_ && m < *min_) {
            out.push_back({name_, is_count ? "is too short" : "is too small"});
        }
        if (max_ && m > *max_) {
            out.push_back({name_, is_count ? "is too long" : "is too large"});
        }
    }

    if (!allowed_.empty()) {
        std::string s = v.is_string() ? v.get<std::string>() : v.dump();
        bool ok = false;
        for (const auto& a : allowed_)
            if (a == s) { ok = true; break; }
        if (!ok) out.push_back({name_, "is not an allowed value"});
    }

    if (pattern_ && v.is_string()) {
        try {
            std::regex re(*pattern_, std::regex::ECMAScript);
            if (!std::regex_match(v.get<std::string>(), re)) {
                out.push_back({name_, "has an invalid format"});
            }
        } catch (const std::regex_error&) {
            out.push_back({name_, "has an invalid validation pattern"});
        }
    }

    if (custom_) {
        if (auto msg = custom_(v)) out.push_back({name_, *msg});
    }
}

nlohmann::json Result::errors_json() const {
    nlohmann::json fields = nlohmann::json::object();
    for (const auto& e : errors) {
        if (!fields.contains(e.field)) fields[e.field] = nlohmann::json::array();
        fields[e.field].push_back(e.message);
    }
    return nlohmann::json{{"error", "validation failed"}, {"errors", fields}};
}

Result validate(const nlohmann::json& doc, const Schema& schema) {
    Result r;
    for (const auto& f : schema) f.check(doc, r.errors);
    r.ok = r.errors.empty();
    return r;
}

} // namespace socketify::validate
