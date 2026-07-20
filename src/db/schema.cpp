/**
 * @file schema.cpp
 * @brief Schema SQL rendering for SQLite / Postgres / MySQL.
 */

#include "socketify/db/schema.h"

#include <sstream>

namespace socketify::db {

std::string quote_ident(Dialect d, std::string_view name) {
    std::string n(name);
    if (d == Dialect::Mysql) return "`" + n + "`";
    return "\"" + n + "\"";
}

std::string column_type_sql(Dialect d, const Column& c) {
    switch (d) {
        case Dialect::Sqlite:
            switch (c.type) {
                case ColumnType::Integer:
                case ColumnType::BigInt:
                case ColumnType::Boolean: return "INTEGER";
                case ColumnType::Real: return "REAL";
                case ColumnType::Blob: return "BLOB";
                case ColumnType::Timestamp:
                case ColumnType::Text: return "TEXT";
            }
            break;
        case Dialect::Postgres:
            if (c.autoincrement && (c.type == ColumnType::Integer || c.type == ColumnType::BigInt))
                return c.type == ColumnType::BigInt ? "BIGSERIAL" : "SERIAL";
            switch (c.type) {
                case ColumnType::Integer: return "INTEGER";
                case ColumnType::BigInt: return "BIGINT";
                case ColumnType::Real: return "DOUBLE PRECISION";
                case ColumnType::Boolean: return "BOOLEAN";
                case ColumnType::Blob: return "BYTEA";
                case ColumnType::Timestamp: return "TIMESTAMP";
                case ColumnType::Text: return "TEXT";
            }
            break;
        case Dialect::Mysql:
            switch (c.type) {
                case ColumnType::Integer: return "INT";
                case ColumnType::BigInt: return "BIGINT";
                case ColumnType::Real: return "DOUBLE";
                case ColumnType::Boolean: return "TINYINT(1)";
                case ColumnType::Blob: return "BLOB";
                case ColumnType::Timestamp: return "DATETIME";
                case ColumnType::Text: return "TEXT";
            }
            break;
    }
    return "TEXT";
}

std::string Schema::to_sql(Dialect dialect) const {
    std::ostringstream oss;
    oss << "CREATE TABLE IF NOT EXISTS " << quote_ident(dialect, table_) << " (\n";
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        const auto& c = columns_[i];
        if (i) oss << ",\n";
        oss << "  " << quote_ident(dialect, c.name) << " " << column_type_sql(dialect, c);
        if (c.primary) {
            oss << " PRIMARY KEY";
            if (c.autoincrement) {
                if (dialect == Dialect::Sqlite) oss << " AUTOINCREMENT";
                else if (dialect == Dialect::Mysql) oss << " AUTO_INCREMENT";
                // Postgres SERIAL already implies
            }
        } else {
            if (c.not_null) oss << " NOT NULL";
            if (c.unique) oss << " UNIQUE";
        }
        if (!c.default_sql.empty() && !(c.autoincrement && dialect == Dialect::Postgres)) {
            oss << " DEFAULT " << c.default_sql;
        }
    }
    for (const auto& fk : fks_) {
        oss << ",\n  FOREIGN KEY (" << quote_ident(dialect, fk.column) << ") REFERENCES "
            << quote_ident(dialect, fk.ref_table) << "(" << quote_ident(dialect, fk.ref_column)
            << ")";
        if (fk.on_delete_cascade) oss << " ON DELETE CASCADE";
    }
    oss << "\n);\n";

    for (const auto& ix : indexes_) {
        oss << "CREATE " << (ix.unique ? "UNIQUE " : "") << "INDEX ";
        if (dialect != Dialect::Mysql) oss << "IF NOT EXISTS ";
        oss << quote_ident(dialect, ix.name) << " ON " << quote_ident(dialect, table_) << " (";
        for (std::size_t i = 0; i < ix.columns.size(); ++i) {
            if (i) oss << ", ";
            oss << quote_ident(dialect, ix.columns[i]);
        }
        oss << ");\n";
    }
    return oss.str();
}

} // namespace socketify::db
