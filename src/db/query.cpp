/**
 * @file query.cpp
 * @brief Fluent SQL query builder.
 */

#include "socketify/db/query.h"
#include "socketify/db/database.h"
#include "socketify/db/schema.h"

#include <cmath>
#include <sstream>

namespace socketify::db {

Query::Query(Database* db, std::string table) : db_(db), table_(std::move(table)) {}

Query& Query::select(std::vector<std::string> cols) {
    select_cols_ = std::move(cols);
    return *this;
}

Query& Query::distinct() {
    distinct_ = true;
    return *this;
}

Query& Query::where(std::string column, WhereOp op, nlohmann::json value) {
    wheres_.push_back({false, std::move(column), op, std::move(value)});
    return *this;
}

Query& Query::or_where(std::string column, WhereOp op, nlohmann::json value) {
    wheres_.push_back({true, std::move(column), op, std::move(value)});
    return *this;
}

Query& Query::where_in(std::string column, nlohmann::json values) {
    return where(std::move(column), WhereOp::In, std::move(values));
}

Query& Query::where_not_in(std::string column, nlohmann::json values) {
    return where(std::move(column), WhereOp::NotIn, std::move(values));
}

Query& Query::where_null(std::string column) {
    return where(std::move(column), WhereOp::IsNull, nullptr);
}

Query& Query::where_not_null(std::string column) {
    return where(std::move(column), WhereOp::IsNotNull, nullptr);
}

Query& Query::or_where_in(std::string column, nlohmann::json values) {
    return or_where(std::move(column), WhereOp::In, std::move(values));
}

Query& Query::or_where_null(std::string column) {
    return or_where(std::move(column), WhereOp::IsNull, nullptr);
}

Query& Query::or_where_not_null(std::string column) {
    return or_where(std::move(column), WhereOp::IsNotNull, nullptr);
}

Query& Query::order_by(std::string column, bool asc) {
    orders_.push_back({std::move(column), asc});
    return *this;
}

Query& Query::limit(int n) {
    limit_ = n;
    return *this;
}

Query& Query::offset(int n) {
    offset_ = n;
    return *this;
}

Query& Query::join(std::string table, std::string on_sql, std::string type) {
    joins_.push_back({std::move(type), std::move(table), std::move(on_sql)});
    return *this;
}

Query& Query::group_by(std::vector<std::string> cols) {
    group_cols_ = std::move(cols);
    return *this;
}

Query& Query::having(std::string expr, Params params) {
    having_sql_ = std::move(expr);
    having_params_ = std::move(params);
    return *this;
}

namespace {

const char* op_sql(WhereOp op) {
    switch (op) {
        case WhereOp::Eq: return "=";
        case WhereOp::Ne: return "!=";
        case WhereOp::Lt: return "<";
        case WhereOp::Lte: return "<=";
        case WhereOp::Gt: return ">";
        case WhereOp::Gte: return ">=";
        case WhereOp::Like: return "LIKE";
        case WhereOp::In: return "IN";
        case WhereOp::NotIn: return "NOT IN";
        case WhereOp::IsNull:
        case WhereOp::IsNotNull: return "";
    }
    return "=";
}

} // namespace

void Query::append_where_(std::string& sql, Params& params) const {
    if (wheres_.empty()) return;
    sql += " WHERE ";
    for (std::size_t i = 0; i < wheres_.size(); ++i) {
        const auto& w = wheres_[i];
        if (i) sql += w.or_combine ? " OR " : " AND ";
        sql += quote_ident(db_->dialect(), w.column);

        if (w.op == WhereOp::IsNull) {
            sql += " IS NULL";
            continue;
        }
        if (w.op == WhereOp::IsNotNull) {
            sql += " IS NOT NULL";
            continue;
        }

        if (w.op == WhereOp::In || w.op == WhereOp::NotIn) {
            sql += " ";
            sql += op_sql(w.op);
            if (!w.value.is_array() || w.value.empty()) {
                // IN () is always false; NOT IN () is always true.
                sql += w.op == WhereOp::In ? " (1=0)" : " (1=1)";
            } else {
                sql += " (";
                for (std::size_t j = 0; j < w.value.size(); ++j) {
                    if (j) sql += ", ";
                    sql += "?";
                    params.push_back(w.value[j]);
                }
                sql += ")";
            }
            continue;
        }

        if (w.value.is_null()) {
            sql += w.op == WhereOp::Ne ? " IS NOT NULL" : " IS NULL";
            continue;
        }

        sql += " ";
        sql += op_sql(w.op);
        sql += " ?";
        params.push_back(w.value);
    }
}

std::pair<std::string, Params> Query::compile_select() const {
    Params params;
    std::string sql = "SELECT ";
    if (distinct_) sql += "DISTINCT ";
    if (select_cols_.empty()) {
        sql += "*";
    } else {
        for (std::size_t i = 0; i < select_cols_.size(); ++i) {
            if (i) sql += ", ";
            sql += quote_ident(db_->dialect(), select_cols_[i]);
        }
    }
    sql += " FROM ";
    sql += quote_ident(db_->dialect(), table_);
    for (const auto& j : joins_) {
        sql += " ";
        sql += j.type;
        sql += " JOIN ";
        sql += quote_ident(db_->dialect(), j.table);
        sql += " ON ";
        sql += j.on_sql;
    }
    append_where_(sql, params);
    if (!group_cols_.empty()) {
        sql += " GROUP BY ";
        for (std::size_t i = 0; i < group_cols_.size(); ++i) {
            if (i) sql += ", ";
            sql += quote_ident(db_->dialect(), group_cols_[i]);
        }
    }
    if (!having_sql_.empty()) {
        sql += " HAVING ";
        sql += having_sql_;
        for (const auto& p : having_params_) params.push_back(p);
    }
    if (!orders_.empty()) {
        sql += " ORDER BY ";
        for (std::size_t i = 0; i < orders_.size(); ++i) {
            if (i) sql += ", ";
            sql += quote_ident(db_->dialect(), orders_[i].column);
            sql += orders_[i].asc ? " ASC" : " DESC";
        }
    }
    if (limit_ >= 0) {
        sql += " LIMIT ";
        sql += std::to_string(limit_);
    }
    if (offset_ >= 0) {
        sql += " OFFSET ";
        sql += std::to_string(offset_);
    }
    return {sql, params};
}

Rows Query::get() {
    auto [sql, params] = compile_select();
    return db_->query(sql, params);
}

std::optional<Row> Query::first() {
    limit_ = 1;
    auto rows = get();
    if (rows.empty()) return std::nullopt;
    return rows.front();
}

std::int64_t Query::count() {
    Params params;
    std::string sql = "SELECT COUNT(*) AS ";
    sql += quote_ident(db_->dialect(), "c");
    sql += " FROM ";
    sql += quote_ident(db_->dialect(), table_);
    for (const auto& j : joins_) {
        sql += " ";
        sql += j.type;
        sql += " JOIN ";
        sql += quote_ident(db_->dialect(), j.table);
        sql += " ON ";
        sql += j.on_sql;
    }
    append_where_(sql, params);
    auto rows = db_->query(sql, params);
    if (rows.empty()) return 0;
    const auto& v = rows[0]["c"];
    if (v.is_number_integer()) return v.get<std::int64_t>();
    if (v.is_number()) return static_cast<std::int64_t>(v.get<double>());
    if (v.is_string()) return std::stoll(v.get<std::string>());
    return 0;
}

bool Query::exists() { return count() > 0; }

std::vector<nlohmann::json> Query::pluck(std::string column) {
    const std::string col = column;
    auto rows = select({col}).get();
    std::vector<nlohmann::json> out;
    out.reserve(rows.size());
    for (const auto& r : rows) {
        auto it = r.find(col);
        out.push_back(it != r.end() ? *it : nlohmann::json{});
    }
    return out;
}

std::int64_t Query::aggregate_(std::string_view fn, std::string column) const {
    Params params;
    std::string sql = "SELECT ";
    sql += std::string(fn);
    sql += "(";
    sql += quote_ident(db_->dialect(), column);
    sql += ") AS ";
    sql += quote_ident(db_->dialect(), "agg");
    sql += " FROM ";
    sql += quote_ident(db_->dialect(), table_);
    for (const auto& j : joins_) {
        sql += " ";
        sql += j.type;
        sql += " JOIN ";
        sql += quote_ident(db_->dialect(), j.table);
        sql += " ON ";
        sql += j.on_sql;
    }
    append_where_(sql, params);
    auto rows = db_->query(sql, params);
    if (rows.empty() || !rows[0].contains("agg") || rows[0]["agg"].is_null()) return 0;
    const auto& v = rows[0]["agg"];
    if (v.is_number_integer()) return v.get<std::int64_t>();
    if (v.is_number()) return static_cast<std::int64_t>(std::lround(v.get<double>()));
    if (v.is_string()) return std::stoll(v.get<std::string>());
    return 0;
}

std::int64_t Query::sum(std::string column) { return aggregate_("SUM", std::move(column)); }
std::int64_t Query::avg(std::string column) { return aggregate_("AVG", std::move(column)); }
std::int64_t Query::min(std::string column) { return aggregate_("MIN", std::move(column)); }
std::int64_t Query::max(std::string column) { return aggregate_("MAX", std::move(column)); }

Page Query::paginate(int page, int per_page) {
    if (page < 1) page = 1;
    if (per_page < 1) per_page = 15;
    Page p;
    p.page = page;
    p.per_page = per_page;
    p.total = count();
    p.total_pages = p.total > 0 ? static_cast<int>((p.total + per_page - 1) / per_page) : 0;
    offset_ = (page - 1) * per_page;
    limit_ = per_page;
    p.rows = get();
    return p;
}

std::int64_t Query::update(const Row& values) {
    Params params;
    std::string sql = "UPDATE ";
    sql += quote_ident(db_->dialect(), table_);
    sql += " SET ";
    bool first = true;
    for (auto it = values.begin(); it != values.end(); ++it) {
        if (it.key() == "id") continue;
        if (!first) sql += ", ";
        first = false;
        sql += quote_ident(db_->dialect(), it.key());
        sql += " = ?";
        params.push_back(it.value());
    }
    if (first) return 0;
    append_where_(sql, params);
    return db_->exec(sql, params);
}

std::int64_t Query::destroy() {
    Params params;
    std::string sql = "DELETE FROM ";
    sql += quote_ident(db_->dialect(), table_);
    append_where_(sql, params);
    return db_->exec(sql, params);
}

std::int64_t Query::upsert(const Row& values, std::vector<std::string> conflict_cols) {
    if (conflict_cols.empty()) throw Error("upsert requires at least one conflict column");

    std::string cols;
    std::string placeholders;
    Params params;
    bool first = true;
    for (auto it = values.begin(); it != values.end(); ++it) {
        if (it.key() == "id" && (it.value().is_null() || it.value() == 0)) continue;
        if (!first) {
            cols += ", ";
            placeholders += ", ";
        }
        first = false;
        cols += quote_ident(db_->dialect(), it.key());
        placeholders += "?";
        params.push_back(it.value());
    }
    if (first) return 0;

    std::string sql = "INSERT INTO " + quote_ident(db_->dialect(), table_) + " (" + cols +
                      ") VALUES (" + placeholders + ") ON CONFLICT (";
    for (std::size_t i = 0; i < conflict_cols.size(); ++i) {
        if (i) sql += ", ";
        sql += quote_ident(db_->dialect(), conflict_cols[i]);
    }
    sql += ") DO UPDATE SET ";
    first = true;
    for (auto it = values.begin(); it != values.end(); ++it) {
        if (it.key() == "id") continue;
        bool is_conflict = false;
        for (const auto& c : conflict_cols)
            if (c == it.key()) { is_conflict = true; break; }
        if (is_conflict) continue;
        if (!first) sql += ", ";
        first = false;
        sql += quote_ident(db_->dialect(), it.key());
        sql += " = excluded.";
        sql += quote_ident(db_->dialect(), it.key());
    }
    return db_->insert(sql, params);
}

std::optional<Row> Query::first_or_create(const Row& attrs) {
    Query q(db_, table_);
    for (auto it = attrs.begin(); it != attrs.end(); ++it) {
        if (it.key() == "id") continue;
        q = q.where_eq(it.key(), it.value());
    }
    if (auto found = q.first()) return *found;

    std::string cols;
    std::string placeholders;
    Params params;
    bool first = true;
    for (auto it = attrs.begin(); it != attrs.end(); ++it) {
        if (it.key() == "id" && (it.value().is_null() || it.value() == 0)) continue;
        if (!first) {
            cols += ", ";
            placeholders += ", ";
        }
        first = false;
        cols += quote_ident(db_->dialect(), it.key());
        placeholders += "?";
        params.push_back(it.value());
    }
    std::string sql = "INSERT INTO " + quote_ident(db_->dialect(), table_) + " (" + cols +
                      ") VALUES (" + placeholders + ")";
    db_->insert(sql, params);
    return q.first();
}

std::optional<Row> Query::update_or_create(const Row& match, const Row& values) {
    Query q(db_, table_);
    for (auto it = match.begin(); it != match.end(); ++it) {
        if (it.key() == "id") continue;
        q = q.where_eq(it.key(), it.value());
    }
    if (auto found = q.first()) {
        Row patch = values;
        q.update(patch);
        return q.first();
    }
    Row merged = match;
    for (auto it = values.begin(); it != values.end(); ++it) merged[it.key()] = it.value();
    return first_or_create(merged);
}

} // namespace socketify::db
