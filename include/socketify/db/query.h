#pragma once
/**
 * @file query.h
 * @brief Fluent SQL query builder.
 */

#include "socketify/db/engine.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace socketify::db {

class Database;

enum class WhereOp : std::uint8_t { Eq, Ne, Lt, Lte, Gt, Gte, Like, In };

struct WhereClause {
    bool or_combine{false};
    std::string column;
    WhereOp op{WhereOp::Eq};
    nlohmann::json value;
};

struct JoinClause {
    std::string type{"INNER"}; // INNER / LEFT
    std::string table;
    std::string on_sql;
};

class Query {
public:
    Query(Database* db, std::string table);

    Query& select(std::vector<std::string> cols);
    Query& where(std::string column, WhereOp op, nlohmann::json value);
    Query& where_eq(std::string column, nlohmann::json value) {
        return where(std::move(column), WhereOp::Eq, std::move(value));
    }
    Query& or_where(std::string column, WhereOp op, nlohmann::json value);
    Query& order_by(std::string column, bool asc = true);
    Query& limit(int n);
    Query& offset(int n);
    Query& join(std::string table, std::string on_sql, std::string type = "INNER");

    Rows get();
    std::optional<Row> first();
    std::int64_t count();
    bool exists();

    /** @brief UPDATE SET … WHERE (current filters). */
    std::int64_t update(const Row& values);
    /** @brief DELETE WHERE (current filters). */
    std::int64_t destroy();

    std::pair<std::string, Params> compile_select() const;

private:
    Database* db_;
    std::string table_;
    std::vector<std::string> select_cols_;
    std::vector<WhereClause> wheres_;
    std::vector<JoinClause> joins_;
    std::string order_col_;
    bool order_asc_{true};
    int limit_{-1};
    int offset_{-1};

    void append_where_(std::string& sql, Params& params) const;
};

} // namespace socketify::db
