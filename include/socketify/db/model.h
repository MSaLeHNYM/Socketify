#pragma once
/**
 * @file model.h
 * @brief ActiveRecord-style SQL model (CRTP).
 */

#include "socketify/db/database.h"
#include "socketify/db/query.h"
#include "socketify/db/relation.h"
#include "socketify/db/schema.h"
#include "socketify/db/validation.h"

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace socketify::db {

class Record {
public:
    Record() = default;
    Record(Database* db, std::string table, Row attrs)
        : db_(db), table_(std::move(table)), attrs_(std::move(attrs)) {}

    Database& db() { return *db_; }
    const std::string& table() const noexcept { return table_; }
    const Row& attrs() const noexcept { return attrs_; }
    Row& attrs() noexcept { return attrs_; }

    bool has(std::string_view key) const { return attrs_.contains(std::string(key)); }

    template <typename T>
    T get(std::string_view key) const {
        return attrs_.at(std::string(key)).get<T>();
    }

    template <typename T>
    T get_or(std::string_view key, T fallback) const {
        auto it = attrs_.find(std::string(key));
        if (it == attrs_.end() || it->is_null()) return fallback;
        return it->get<T>();
    }

    std::int64_t id() const { return get_or<std::int64_t>("id", 0); }

    void set(std::string_view key, nlohmann::json value) {
        attrs_[std::string(key)] = std::move(value);
        dirty_ = true;
    }

    void update(const Row& patch);
    bool save();
    bool destroy();

    /** @brief Load related rows by association name (see Model::boot relations). */
    Rows related(std::string_view name);

protected:
    Database* db_{nullptr};
    std::string table_;
    Row attrs_ = Row::object();
    bool dirty_{false};
};

using HookFn = std::function<void(Record&)>;

struct ModelMeta {
    std::vector<FieldValidator> validators;
    std::vector<HookFn> before_create;
    std::vector<HookFn> after_create;
    std::vector<HookFn> before_save;
    std::vector<HookFn> after_save;
    std::vector<HookFn> before_update;
    std::vector<HookFn> after_update;
    std::vector<HookFn> before_destroy;
    std::vector<HookFn> after_destroy;
    RelationRegistry relations;
    bool booted{false};
};

template <typename Derived>
class Model : public Record {
public:
    using Record::Record;

    static ModelMeta& meta() {
        static ModelMeta m;
        return m;
    }

    static void ensure_boot() {
        static std::once_flag once;
        std::call_once(once, [] {
            Derived::boot();
            meta().booted = true;
        });
    }

    static void boot() {}

    static void validates(std::string field, ValidatorFn fn) {
        meta().validators.push_back({std::move(field), std::move(fn)});
    }
    template <typename... Rest>
    static void validates(std::string field, ValidatorFn first, Rest... rest) {
        validates(field, first);
        (validates(field, rest), ...);
    }

    static void before_create(HookFn fn) { meta().before_create.push_back(std::move(fn)); }
    static void after_create(HookFn fn) { meta().after_create.push_back(std::move(fn)); }
    static void before_save(HookFn fn) { meta().before_save.push_back(std::move(fn)); }
    static void after_save(HookFn fn) { meta().after_save.push_back(std::move(fn)); }
    static void before_update(HookFn fn) { meta().before_update.push_back(std::move(fn)); }
    static void after_update(HookFn fn) { meta().after_update.push_back(std::move(fn)); }
    static void before_destroy(HookFn fn) { meta().before_destroy.push_back(std::move(fn)); }
    static void after_destroy(HookFn fn) { meta().after_destroy.push_back(std::move(fn)); }

    template <typename Related>
    static void has_many(std::string foreign_key, std::string name = {}) {
        if (name.empty()) name = std::string(Related::table);
        has_many_on(std::string(Related::table), std::move(foreign_key), std::move(name));
    }
    static void has_many_on(std::string related_table, std::string foreign_key,
                            std::string name) {
        meta().relations.add({RelationKind::HasMany, std::move(name), std::move(related_table),
                              std::move(foreign_key), "id", {}, "id"});
    }
    template <typename Related>
    static void belongs_to(std::string foreign_key, std::string name = {}) {
        if (name.empty()) name = std::string(Related::table);
        belongs_to_on(std::string(Related::table), std::move(foreign_key), std::move(name));
    }
    static void belongs_to_on(std::string related_table, std::string foreign_key,
                              std::string name) {
        meta().relations.add({RelationKind::BelongsTo, std::move(name), std::move(related_table),
                              std::move(foreign_key), "id", {}, "id"});
    }
    template <typename Related>
    static void has_one(std::string foreign_key, std::string name = {}) {
        if (name.empty()) name = std::string(Related::table);
        meta().relations.add({RelationKind::HasOne, name, std::string(Related::table),
                              std::move(foreign_key), "id", {}, "id"});
    }
    template <typename Related>
    static void belongs_to_many(std::string pivot, std::string foreign_key,
                                std::string related_key, std::string name = {}) {
        if (name.empty()) name = std::string(Related::table);
        meta().relations.add({RelationKind::BelongsToMany, name, std::string(Related::table),
                              std::move(foreign_key), "id", std::move(pivot),
                              std::move(related_key)});
    }

    static void migrate_schema(Database& db) {
        ensure_boot();
        db.create_table(Derived::schema());
    }

    static std::shared_ptr<Derived> create(Database& db, Row attrs) {
        ensure_boot();
        auto rec = std::make_shared<Derived>();
        rec->db_ = &db;
        rec->table_ = std::string(Derived::table);
        rec->attrs_ = std::move(attrs);
        if (!rec->attrs_.is_object()) rec->attrs_ = Row::object();

        auto errs = run_validators(meta().validators, rec->attrs_);
        if (!errs.empty()) {
            throw Error("validation failed: " + errs.front().field + " " + errs.front().message);
        }
        for (auto& h : meta().before_create) h(*rec);
        for (auto& h : meta().before_save) h(*rec);

        // timestamps
        auto schema = Derived::schema();
        bool has_created = false, has_updated = false;
        for (auto& c : schema.columns()) {
            if (c.name == "created_at") has_created = true;
            if (c.name == "updated_at") has_updated = true;
        }
        // Insert via query builder helpers in model.cpp style
        rec->persist_insert_();

        for (auto& h : meta().after_create) h(*rec);
        for (auto& h : meta().after_save) h(*rec);
        (void)has_created;
        (void)has_updated;
        return rec;
    }

    static std::optional<std::shared_ptr<Derived>> find(Database& db, std::int64_t id) {
        ensure_boot();
        auto rows = Query(&db, std::string(Derived::table)).where_eq("id", id).limit(1).get();
        if (rows.empty()) return std::nullopt;
        auto rec = std::make_shared<Derived>();
        rec->db_ = &db;
        rec->table_ = std::string(Derived::table);
        rec->attrs_ = std::move(rows.front());
        return rec;
    }

    static std::shared_ptr<Derived> find_or_fail(Database& db, std::int64_t id) {
        auto r = find(db, id);
        if (!r) throw Error("record not found");
        return *r;
    }

    static Query query(Database& db) {
        ensure_boot();
        return Query(&db, std::string(Derived::table));
    }

    static std::vector<std::shared_ptr<Derived>> all(Database& db) {
        auto rows = query(db).get();
        std::vector<std::shared_ptr<Derived>> out;
        out.reserve(rows.size());
        for (auto& row : rows) {
            auto rec = std::make_shared<Derived>();
            rec->db_ = &db;
            rec->table_ = std::string(Derived::table);
            rec->attrs_ = std::move(row);
            out.push_back(std::move(rec));
        }
        return out;
    }

    Rows related(std::string_view name) {
        ensure_boot();
        auto* rel = meta().relations.find(name);
        if (!rel) throw Error(std::string("unknown relation: ") + std::string(name));
        switch (rel->kind) {
            case RelationKind::HasMany:
            case RelationKind::HasOne:
                return Query(db_, rel->related_table)
                    .where_eq(rel->foreign_key, id())
                    .get();
            case RelationKind::BelongsTo: {
                auto fk = attrs_.contains(rel->foreign_key) ? attrs_[rel->foreign_key]
                                                            : nlohmann::json{};
                return Query(db_, rel->related_table)
                    .where_eq(rel->related_key, fk)
                    .limit(1)
                    .get();
            }
            case RelationKind::BelongsToMany: {
                // SELECT t.* FROM related t
                // INNER JOIN pivot p ON p.related_key = t.id
                // WHERE p.foreign_key = ?
                auto q = quote_ident(db_->dialect(), rel->related_table);
                auto p = quote_ident(db_->dialect(), rel->pivot_table);
                auto sql = "SELECT " + q + ".* FROM " + q + " INNER JOIN " + p + " ON " + p +
                           "." + quote_ident(db_->dialect(), rel->related_key) + " = " + q +
                           "." + quote_ident(db_->dialect(), "id") + " WHERE " + p + "." +
                           quote_ident(db_->dialect(), rel->foreign_key) + " = ?";
                return db_->query(sql, {id()});
            }
        }
        return {};
    }

private:
    void persist_insert_();
};

} // namespace socketify::db

#include "socketify/db/model_impl.h"
