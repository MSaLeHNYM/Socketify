#pragma once
// socketify/body.h — Body parsing utilities

#include "request.h"

#include <nlohmann/json.hpp>
#include <optional>

namespace socketify {

// ---- JSON Body Parser ----

// Tries to parse the request body as JSON.
//
// Returns:
// - A valid nlohmann::json object on success.
// - std::nullopt if the body is empty, not valid JSON, or if the
//   Content-Type is not "application/json".
std::optional<nlohmann::json> parse_json_body(const Request& req);


// ---- URL-encoded Form Parser ----

// Tries to parse the request body as a URL-encoded form.
// (e.g., "key1=value1&key2=value2")
//
// Returns:
// - A map of key-value pairs on success.
// - std::nullopt if the body is empty or if the Content-Type is not
//   "application/x-www-form-urlencoded".
std::optional<ParamMap> parse_form_body(const Request& req);

} // namespace socketify