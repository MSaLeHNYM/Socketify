#pragma once
/**
 * @file model_impl.h
 * @brief Out-of-line template helpers for Model<Derived>.
 */

#include "socketify/db/query.h"

namespace socketify::db {

template <typename Derived>
void Model<Derived>::persist_insert_() {
    std::string cols;
    std::string placeholders;
    Params params;
    bool first = true;
    for (auto it = attrs_.begin(); it != attrs_.end(); ++it) {
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
    if (cols.empty()) {
        // Insert default row
        std::string sql = "INSERT INTO " + quote_ident(db_->dialect(), table_) + " DEFAULT VALUES";
        if (db_->dialect() == Dialect::Mysql) {
            sql = "INSERT INTO " + quote_ident(db_->dialect(), table_) + " () VALUES ()";
        }
        auto id = db_->insert(sql, {});
        if (id > 0) attrs_["id"] = id;
    } else {
        std::string sql = "INSERT INTO " + quote_ident(db_->dialect(), table_) + " (" + cols +
                          ") VALUES (" + placeholders + ")";
        auto id = db_->insert(sql, params);
        if (id > 0) attrs_["id"] = id;
        else if (!attrs_.contains("id")) {
            // Postgres RETURNING handled inside driver insert when possible
        }
    }
    dirty_ = false;
}

} // namespace socketify::db
