#pragma once
/**
 * @file validate.h
 * @brief Declarative validation for JSON request bodies.
 *
 * Build a Schema from fluent Field rules and validate an nlohmann::json object.
 * Designed for HTTP handlers: the Result carries structured, client-friendly
 * errors that serialize straight to a JSON response.
 *
 * @code
 * using namespace socketify;
 * static const validate::Schema kSignup = {
 *     validate::field("email").required().string().email(),
 *     validate::field("age").integer().min(13).max(120),
 *     validate::field("role").one_of({"user", "admin"}).optional(),
 * };
 *
 * auto doc = req.json();
 * auto r = validate::validate(doc.value_or(nlohmann::json::object()), kSignup);
 * if (!r.ok) { res.status(Status::UnprocessableEntity).json(r.errors_json()); return; }
 * @endcode
 */

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace socketify::validate {

/** @brief A single validation failure. */
struct Error {
    std::string field;   ///< Field name that failed.
    std::string message; ///< Human-readable reason.
};

/** @brief Outcome of validating a document against a Schema. */
struct Result {
    bool ok{true};
    std::vector<Error> errors;

    /** @brief All failures as {"errors": {field: [messages...]}}. */
    nlohmann::json errors_json() const;

    /** @brief First error message (empty when ok). */
    std::string first_message() const {
        return errors.empty() ? std::string{} : errors.front().message;
    }
};

/** @brief Expected JSON type for a field (used by type() rules). */
enum class Type : std::uint8_t { Any, String, Integer, Number, Boolean, Array, Object };

/**
 * @brief One field's validation rules, built fluently.
 *
 * By default a field is optional; call required() to reject missing/null.
 * Type and constraint checks only run when the value is present.
 */
class Field {
public:
    explicit Field(std::string name) : name_(std::move(name)) {}

    Field& required() { required_ = true; return *this; }
    Field& optional() { required_ = false; return *this; }

    Field& type(Type t) { type_ = t; return *this; }
    Field& string() { return type(Type::String); }
    Field& integer() { return type(Type::Integer); }
    Field& number() { return type(Type::Number); }
    Field& boolean() { return type(Type::Boolean); }
    Field& array() { return type(Type::Array); }
    Field& object() { return type(Type::Object); }

    /** @brief Minimum string length / array size / numeric value. */
    Field& min(double v) { min_ = v; return *this; }
    /** @brief Maximum string length / array size / numeric value. */
    Field& max(double v) { max_ = v; return *this; }

    /** @brief Require a valid-looking email (implies string). */
    Field& email() { email_ = true; return type(Type::String); }

    /** @brief Restrict to an allowed set of string values. */
    Field& one_of(std::initializer_list<std::string_view> values) {
        for (auto v : values) allowed_.emplace_back(v);
        return *this;
    }

    /** @brief Require the string to match @p pattern (ECMAScript regex). */
    Field& matches(std::string pattern) { pattern_ = std::move(pattern); return *this; }

    /** @brief Custom predicate; return an error message or std::nullopt. */
    Field& custom(std::function<std::optional<std::string>(const nlohmann::json&)> fn) {
        custom_ = std::move(fn);
        return *this;
    }

    /** @brief Validate @p doc, appending any failures to @p out. */
    void check(const nlohmann::json& doc, std::vector<Error>& out) const;

private:
    std::string name_;
    bool required_{false};
    Type type_{Type::Any};
    std::optional<double> min_;
    std::optional<double> max_;
    bool email_{false};
    std::vector<std::string> allowed_;
    std::optional<std::string> pattern_;
    std::function<std::optional<std::string>(const nlohmann::json&)> custom_;
};

/** @brief Start a rule for @p name: `field("email").required().email()`. */
inline Field field(std::string name) { return Field(std::move(name)); }

/** @brief A collection of field rules. */
using Schema = std::vector<Field>;

/** @brief Validate @p doc against @p schema. */
Result validate(const nlohmann::json& doc, const Schema& schema);

} // namespace socketify::validate
