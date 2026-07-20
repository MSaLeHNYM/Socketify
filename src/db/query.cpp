/**
 * @file query.cpp
 * @brief Fluent SQL query builder.
 */

#include "socketify/db/query.h"
#include "socketify/db/database.h"
#include "socketify/db/schema.h"

#include <sstream>

namespace socketify::db {

Query::Query(Database* db, std::string table) : db_(db), table_(std::move(table)) {}

Query& Query::select(std::vector<std::string> cols) {
    select_cols_ = std::move(cols);
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

Query& Query::order_by(std::string column, bool asc) {
    order_col_ = std::move(column);
    order_asc_ = asc;
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
        sql += " ";
        sql += op_sql(w.op);
        if (w.op == WhereOp::In && w.value.is_array()) {
            sql += " (";
            for (std::size_t j = 0; j < w.value.size(); ++j) {
                if (j) sql += ", ";
                sql += "?";
                params.push_back(w.value[j]);
            }
            sql += ")";
        } else {
            sql += " ?";
            params.push_back(w.value);
        }
    }
}

std::pair<std::string, Params> Query::compile_select() const {
    Params params;
    std::string sql = "SELECT ";
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
    if (!order_col_.empty()) {
        sql += " ORDER BY ";
        sql += quote_ident(db_->dialect(), order_col_);
        sql += order_asc_ ? " ASC" : " DESC";
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
    return rows[0]["c"].get<std::int64_t>();
}

bool Query::exists() { return count() > 0; }

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
    db_->exec(sql, params);
    return 1;
}

std::int64_t Query::destroy() {
    Params params;
    std::string sql = "DELETE FROM ";
    sql += quote_ident(db_->dialect(), table_);
    append_where_(sql, params);
    db_->exec(sql, params);
    return 1;
}

} // namespace socketify::db
