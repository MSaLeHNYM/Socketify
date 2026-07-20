#pragma once
/**
 * @file document.h
 * @brief ActiveRecord-style MongoDB document model (CRTP).
 */

#include "socketify/db/database.h"
#include "socketify/db/validation.h"

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace socketify::db {

struct IndexSpec {
    Row keys = Row::object();
    bool unique{false};
    static IndexSpec asc(std::string_view field) {
        IndexSpec i;
        i.keys[std::string(field)] = 1;
        return i;
    }
    static IndexSpec desc(std::string_view field) {
        IndexSpec i;
        i.keys[std::string(field)] = -1;
        return i;
    }
    IndexSpec& set_unique(bool u = true) {
        unique = u;
        return *this;
    }
};

struct DocumentMeta {
    std::vector<FieldValidator> validators;
    std::vector<std::function<void(Row&)>> before_save;
    std::vector<std::function<void(Row&)>> after_save;
    std::vector<IndexSpec> indexes;
    bool booted{false};
};

template <typename Derived>
class Document {
public:
    Document() = default;
    Document(Database* db, Row attrs) : db_(db), attrs_(std::move(attrs)) {}

    static DocumentMeta& meta() {
        static DocumentMeta m;
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

    static void before_save(std::function<void(Row&)> fn) {
        meta().before_save.push_back(std::move(fn));
    }
    static void after_save(std::function<void(Row&)> fn) {
        meta().after_save.push_back(std::move(fn));
    }
    static void index(IndexSpec spec) { meta().indexes.push_back(std::move(spec)); }

    static void ensure_indexes(Database& db) {
        ensure_boot();
        auto& eng = db.documents();
        for (auto& ix : meta().indexes) {
            eng.create_index(Derived::collection, ix.keys, ix.unique);
        }
    }

    const Row& attrs() const noexcept { return attrs_; }
    Row& attrs() noexcept { return attrs_; }

    template <typename T>
    T get(std::string_view key) const {
        return attrs_.at(std::string(key)).get<T>();
    }

    static std::shared_ptr<Derived> create(Database& db, Row attrs) {
        ensure_boot();
        auto errs = run_validators(meta().validators, attrs);
        if (!errs.empty()) {
            throw Error("validation failed: " + errs.front().field + " " + errs.front().message);
        }
        for (auto& h : meta().before_save) h(attrs);
        db.documents().insert_one(Derived::collection, attrs);
        for (auto& h : meta().after_save) h(attrs);
        auto rec = std::make_shared<Derived>();
        rec->db_ = &db;
        rec->attrs_ = std::move(attrs);
        return rec;
    }

    static std::optional<std::shared_ptr<Derived>> find_one(Database& db, const Row& filter) {
        ensure_boot();
        auto row = db.documents().find_one(Derived::collection, filter);
        if (!row) return std::nullopt;
        auto rec = std::make_shared<Derived>();
        rec->db_ = &db;
        rec->attrs_ = std::move(*row);
        return rec;
    }

    static std::vector<std::shared_ptr<Derived>> find(Database& db, const Row& filter,
                                                      int limit = 0) {
        ensure_boot();
        auto rows = db.documents().find(Derived::collection, filter, limit);
        std::vector<std::shared_ptr<Derived>> out;
        for (auto& r : rows) {
            auto rec = std::make_shared<Derived>();
            rec->db_ = &db;
            rec->attrs_ = std::move(r);
            out.push_back(std::move(rec));
        }
        return out;
    }

    bool update(const Row& patch) {
        ensure_boot();
        for (auto it = patch.begin(); it != patch.end(); ++it) attrs_[it.key()] = it.value();
        auto errs = run_validators(meta().validators, attrs_);
        if (!errs.empty()) {
            throw Error("validation failed: " + errs.front().field + " " + errs.front().message);
        }
        for (auto& h : meta().before_save) h(attrs_);
        Row filter;
        if (attrs_.contains("_id")) filter["_id"] = attrs_["_id"];
        else if (attrs_.contains("id")) filter["id"] = attrs_["id"];
        else throw Error("document has no id");
        Row set = attrs_;
        set.erase("_id");
        db_->documents().update_many(Derived::collection, filter, set);
        for (auto& h : meta().after_save) h(attrs_);
        return true;
    }

    bool destroy() {
        Row filter;
        if (attrs_.contains("_id")) filter["_id"] = attrs_["_id"];
        else if (attrs_.contains("id")) filter["id"] = attrs_["id"];
        else throw Error("document has no id");
        db_->documents().delete_many(Derived::collection, filter);
        return true;
    }

    /** @brief Populate related docs where related.foreign_key == this.id */
    template <typename Related>
    std::vector<std::shared_ptr<Related>> populate(std::string_view foreign_key) {
        auto id = attrs_.contains("id") ? attrs_["id"] : attrs_["_id"];
        Row filter{{std::string(foreign_key), id}};
        return Related::find(*db_, filter);
    }

private:
    Database* db_{nullptr};
    Row attrs_ = Row::object();

    // Allow CRTP create/find helpers to populate private fields.
    template <typename T>
    friend class Document;
};

} // namespace socketify::db
