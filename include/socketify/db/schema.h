#pragma once
/**
 * @file schema.h
 * @brief Fluent SQL schema / column DSL.
 */

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace socketify::db {

enum class ColumnType : std::uint8_t {
    Integer,
    BigInt,
    Real,
    Text,
    Blob,
    Boolean,
    Timestamp,
};

enum class Dialect : std::uint8_t {
    Sqlite,
    Postgres,
    Mysql,
};

struct ForeignKey {
    std::string column;
    std::string ref_table;
    std::string ref_column{"id"};
    bool on_delete_cascade{false};
};

struct IndexDef {
    std::string name;
    std::vector<std::string> columns;
    bool unique{false};
};

struct Column {
    std::string name;
    ColumnType type{ColumnType::Text};
    bool primary{false};
    bool autoincrement{false};
    bool not_null{false};
    bool unique{false};
    std::string default_sql; // raw SQL fragment, e.g. "CURRENT_TIMESTAMP"
};

class Schema {
public:
    static Schema create(std::string_view table_name) {
        Schema s;
        s.table_ = std::string(table_name);
        return s;
    }

    Schema& integer(std::string_view name) { return add_(name, ColumnType::Integer); }
    Schema& bigint(std::string_view name) { return add_(name, ColumnType::BigInt); }
    Schema& real(std::string_view name) { return add_(name, ColumnType::Real); }
    Schema& text(std::string_view name) { return add_(name, ColumnType::Text); }
    Schema& blob(std::string_view name) { return add_(name, ColumnType::Blob); }
    Schema& boolean(std::string_view name) { return add_(name, ColumnType::Boolean); }
    Schema& timestamp(std::string_view name) { return add_(name, ColumnType::Timestamp); }

    Schema& primary() {
        if (!columns_.empty()) columns_.back().primary = true;
        return *this;
    }
    Schema& autoincrement() {
        if (!columns_.empty()) columns_.back().autoincrement = true;
        return *this;
    }
    Schema& not_null() {
        if (!columns_.empty()) columns_.back().not_null = true;
        return *this;
    }
    Schema& unique() {
        if (!columns_.empty()) columns_.back().unique = true;
        return *this;
    }
    Schema& default_value(std::string_view sql) {
        if (!columns_.empty()) columns_.back().default_sql = std::string(sql);
        return *this;
    }

    Schema& timestamps() {
        timestamp("created_at").not_null().default_value("CURRENT_TIMESTAMP");
        timestamp("updated_at").not_null().default_value("CURRENT_TIMESTAMP");
        return *this;
    }

    Schema& foreign_key(std::string_view column, std::string_view ref_table,
                        std::string_view ref_column = "id", bool cascade = false) {
        fks_.push_back(ForeignKey{std::string(column), std::string(ref_table),
                                  std::string(ref_column), cascade});
        return *this;
    }

    Schema& index(std::string name, std::vector<std::string> cols, bool unique = false) {
        indexes_.push_back(IndexDef{std::move(name), std::move(cols), unique});
        return *this;
    }

    const std::string& table() const noexcept { return table_; }
    const std::vector<Column>& columns() const noexcept { return columns_; }
    const std::vector<ForeignKey>& foreign_keys() const noexcept { return fks_; }
    const std::vector<IndexDef>& indexes() const noexcept { return indexes_; }

    /** @brief Render CREATE TABLE (+ indexes) for @p dialect. */
    std::string to_sql(Dialect dialect) const;

private:
    std::string table_;
    std::vector<Column> columns_;
    std::vector<ForeignKey> fks_;
    std::vector<IndexDef> indexes_;

    Schema& add_(std::string_view name, ColumnType t) {
        columns_.push_back(Column{std::string(name), t, false, false, false, false, {}});
        return *this;
    }
};

/** @brief Quote an identifier for @p dialect. */
std::string quote_ident(Dialect d, std::string_view name);

/** @brief SQL type name for a column in @p dialect. */
std::string column_type_sql(Dialect d, const Column& c);

} // namespace socketify::db
