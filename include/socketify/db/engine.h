#pragma once
/**
 * @file engine.h
 * @brief Low-level SQL / document engine interfaces (internal to drivers).
 */

#include "socketify/db/error.h"
#include "socketify/db/schema.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace socketify::db {

using Row = nlohmann::json;       // object
using Rows = std::vector<Row>;
using Params = std::vector<nlohmann::json>;

struct SqlEngine {
    virtual ~SqlEngine() = default;
    virtual Dialect dialect() const = 0;
    virtual void exec(std::string_view sql, const Params& params = {}) = 0;
    virtual Rows query(std::string_view sql, const Params& params = {}) = 0;
    /** @brief INSERT … ; returns last insert id when available (else 0). */
    virtual std::int64_t insert(std::string_view sql, const Params& params = {}) = 0;
    virtual void begin() = 0;
    virtual void commit() = 0;
    virtual void rollback() = 0;
};

using SqlEnginePtr = std::shared_ptr<SqlEngine>;

struct DocumentEngine {
    virtual ~DocumentEngine() = default;
    virtual void insert_one(std::string_view coll, const Row& doc) = 0;
    virtual std::optional<Row> find_one(std::string_view coll, const Row& filter) = 0;
    virtual Rows find(std::string_view coll, const Row& filter, int limit = 0,
                      int skip = 0, std::string_view sort_field = {},
                      bool sort_asc = true) = 0;
    virtual std::int64_t update_many(std::string_view coll, const Row& filter,
                                     const Row& set_fields) = 0;
    virtual std::int64_t delete_many(std::string_view coll, const Row& filter) = 0;
    virtual std::int64_t count(std::string_view coll, const Row& filter = {}) = 0;
    virtual void create_index(std::string_view coll, const Row& keys, bool unique) = 0;
    virtual void drop_collection(std::string_view coll) = 0;
};

using DocumentEnginePtr = std::shared_ptr<DocumentEngine>;

} // namespace socketify::db
