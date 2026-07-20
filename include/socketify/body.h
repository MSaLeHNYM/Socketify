#pragma once
/**
 * @file body.h
 * @brief Request body parsing: JSON, urlencoded forms and multipart uploads.
 *
 * @code
 * server.Post("/api/items", [](Request& req, Response& res) {
 *     auto j = body::json(req);
 *     if (!j) { res.status(Status::BadRequest).send("bad json\n"); return; }
 *     res.status(Status::Created).json({{"name", (*j)["name"]}});
 * });
 *
 * server.Post("/upload", [](Request& req, Response& res) {
 *     auto form = body::multipart(req);
 *     if (form) for (auto& f : form->files) save(f.filename, f.data);
 *     res.send("ok\n");
 * });
 * @endcode
 */

#include "socketify/request.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace socketify::body {

/**
 * @brief Parse the body as JSON.
 * @return The document, or std::nullopt when the body is empty or invalid.
 * @note Does not require the Content-Type to be application/json.
 */
std::optional<nlohmann::json> json(const Request& req);

/**
 * @brief Parse an application/x-www-form-urlencoded body ("a=1&b=2").
 * @return Decoded key/value map (empty when the body is empty).
 */
ParamMap form(const Request& req);

/** @brief One uploaded file from a multipart/form-data body. */
struct FilePart {
    std::string name;         ///< Form field name.
    std::string filename;     ///< Client-provided file name.
    std::string content_type; ///< Part Content-Type ("" when absent).
    std::string data;         ///< Raw file bytes.
};

/** @brief Parsed multipart/form-data payload. */
struct MultipartData {
    ParamMap fields;             ///< Non-file form fields.
    std::vector<FilePart> files; ///< Uploaded files, in order of appearance.
};

/**
 * @brief Parse a multipart/form-data body.
 * @return Parsed fields/files, or std::nullopt when the request is not
 *         multipart or the payload is malformed.
 */
std::optional<MultipartData> multipart(const Request& req);

/** @brief True when the Content-Type is application/json (params ignored). */
bool is_json(const Request& req);
/** @brief True when the Content-Type is application/x-www-form-urlencoded. */
bool is_form(const Request& req);
/** @brief True when the Content-Type is multipart/form-data. */
bool is_multipart(const Request& req);

} // namespace socketify::body
