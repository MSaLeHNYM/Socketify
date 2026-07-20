#pragma once
/**
 * @file validation.h
 * @brief Model / document attribute validators.
 */

#include <nlohmann/json.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace socketify::db {

struct ValidationError {
    std::string field;
    std::string message;
};

using ValidatorFn = std::function<std::optional<std::string>(const nlohmann::json& value,
                                                              const nlohmann::json& attrs)>;

struct FieldValidator {
    std::string field;
    ValidatorFn fn;
};

inline ValidatorFn required() {
    return [](const nlohmann::json& v, const nlohmann::json&) -> std::optional<std::string> {
        if (v.is_null() || (v.is_string() && v.get<std::string>().empty()))
            return "is required";
        return std::nullopt;
    };
}

inline ValidatorFn email() {
    return [](const nlohmann::json& v, const nlohmann::json&) -> std::optional<std::string> {
        if (!v.is_string()) return "must be a string";
        auto s = v.get<std::string>();
        auto at = s.find('@');
        if (at == std::string::npos || at == 0 || s.find('.', at) == std::string::npos)
            return "must be a valid email";
        return std::nullopt;
    };
}

inline ValidatorFn min_length(std::size_t n) {
    return [n](const nlohmann::json& v, const nlohmann::json&) -> std::optional<std::string> {
        if (!v.is_string()) return std::nullopt;
        if (v.get<std::string>().size() < n) return "is too short";
        return std::nullopt;
    };
}

inline ValidatorFn max_length(std::size_t n) {
    return [n](const nlohmann::json& v, const nlohmann::json&) -> std::optional<std::string> {
        if (!v.is_string()) return std::nullopt;
        if (v.get<std::string>().size() > n) return "is too long";
        return std::nullopt;
    };
}

inline ValidatorFn min_value(double n) {
    return [n](const nlohmann::json& v, const nlohmann::json&) -> std::optional<std::string> {
        if (!v.is_number()) return std::nullopt;
        if (v.get<double>() < n) return "is too small";
        return std::nullopt;
    };
}

inline ValidatorFn max_value(double n) {
    return [n](const nlohmann::json& v, const nlohmann::json&) -> std::optional<std::string> {
        if (!v.is_number()) return std::nullopt;
        if (v.get<double>() > n) return "is too large";
        return std::nullopt;
    };
}

/** @brief Run validators; returns all errors (empty = ok). */
inline std::vector<ValidationError> run_validators(const std::vector<FieldValidator>& vals,
                                                   const nlohmann::json& attrs) {
    std::vector<ValidationError> out;
    for (const auto& v : vals) {
        nlohmann::json val = attrs.contains(v.field) ? attrs.at(v.field) : nlohmann::json{};
        if (auto msg = v.fn(val, attrs)) {
            out.push_back({v.field, *msg});
        }
    }
    return out;
}

} // namespace socketify::db
