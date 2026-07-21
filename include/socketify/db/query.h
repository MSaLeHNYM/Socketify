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

enum class WhereOp : std::uint8_t {
    Eq, Ne, Lt, Lte, Gt, Gte, Like, In, NotIn, IsNull, IsNotNull
};

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

struct OrderClause {
    std::string column;
    bool asc{true};
};

/** @brief Result of Query::paginate(). */
struct Page {
    Rows rows;
    std::int64_t total{0};
    int page{1};
    int per_page{15};
    int total_pages{0};
};

class Query {
public:
    Query(Database* db, std::string table);

    Query& select(std::vector<std::string> cols);
    Query& distinct();
    Query& where(std::string column, WhereOp op, nlohmann::json value);
    Query& where_eq(std::string column, nlohmann::json value) {
        return where(std::move(column), WhereOp::Eq, std::move(value));
    }
    Query& where_in(std::string column, nlohmann::json values);
    Query& where_not_in(std::string column, nlohmann::json values);
    Query& where_null(std::string column);
    Query& where_not_null(std::string column);
    Query& or_where(std::string column, WhereOp op, nlohmann::json value);
    Query& or_where_eq(std::string column, nlohmann::json value) {
        return or_where(std::move(column), WhereOp::Eq, std::move(value));
    }
    Query& or_where_in(std::string column, nlohmann::json values);
    Query& or_where_null(std::string column);
    Query& or_where_not_null(std::string column);
    Query& order_by(std::string column, bool asc = true);
    Query& limit(int n);
    Query& offset(int n);
    Query& join(std::string table, std::string on_sql, std::string type = "INNER");
    Query& group_by(std::vector<std::string> cols);
    Query& having(std::string expr, Params params = {});

    Rows get();
    std::optional<Row> first();
    std::int64_t count();
    bool exists();

    /** @brief Return a single column as a vector of values. */
    std::vector<nlohmann::json> pluck(std::string column);

    std::int64_t sum(std::string column);
    std::int64_t avg(std::string column);
    std::int64_t min(std::string column);
    std::int64_t max(std::string column);

    /** @brief Paginated results (1-based @p page). */
    Page paginate(int page = 1, int per_page = 15);

    /** @brief UPDATE SET … WHERE (current filters). Returns affected row count. */
    std::int64_t update(const Row& values);
    /** @brief DELETE WHERE (current filters). Returns affected row count. */
    std::int64_t destroy();

    /**
     * @brief INSERT … ON CONFLICT … DO UPDATE (SQLite dialect).
     * @return Last insert / affected row id when available.
     */
    std::int64_t upsert(const Row& values, std::vector<std::string> conflict_cols);

    /** @brief Find first row matching @p attrs keys, or INSERT and return it. */
    std::optional<Row> first_or_create(const Row& attrs);

    /** @brief Find by @p match keys; update with @p values or INSERT. */
    std::optional<Row> update_or_create(const Row& match, const Row& values);

    std::pair<std::string, Params> compile_select() const;

private:
    Database* db_;
    std::string table_;
    bool distinct_{false};
    std::vector<std::string> select_cols_;
    std::vector<WhereClause> wheres_;
    std::vector<JoinClause> joins_;
    std::vector<OrderClause> orders_;
    std::vector<std::string> group_cols_;
    std::string having_sql_;
    Params having_params_;
    int limit_{-1};
    int offset_{-1};

    void append_where_(std::string& sql, Params& params) const;
    std::int64_t aggregate_(std::string_view fn, std::string column) const;
};

} // namespace socketify::db
