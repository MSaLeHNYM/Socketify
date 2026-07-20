/**
 * @file mongo_engine.cpp
 * @brief MongoDB DocumentEngine + in-memory engine for uri "memory://".
 */

#include "socketify/db/database.h"
#include "socketify/db/error.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>

#if SOCKETIFY_HAS_MONGO
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/json.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>
#endif

namespace socketify::db {
namespace {

bool match_filter_(const Row& doc, const Row& filter) {
    if (!filter.is_object() || filter.empty()) return true;
    for (auto it = filter.begin(); it != filter.end(); ++it) {
        if (!doc.contains(it.key()) || doc.at(it.key()) != it.value()) return false;
    }
    return true;
}

class MemoryDocumentEngine final : public DocumentEngine {
public:
    void insert_one(std::string_view coll, const Row& doc) override {
        std::lock_guard lk(mu_);
        auto& c = colls_[std::string(coll)];
        Row copy = doc;
        if (!copy.contains("id") && !copy.contains("_id")) {
            copy["id"] = ++seq_;
            copy["_id"] = copy["id"];
        }
        c.push_back(std::move(copy));
    }

    std::optional<Row> find_one(std::string_view coll, const Row& filter) override {
        std::lock_guard lk(mu_);
        auto it = colls_.find(std::string(coll));
        if (it == colls_.end()) return std::nullopt;
        for (auto& d : it->second) {
            if (match_filter_(d, filter)) return d;
        }
        return std::nullopt;
    }

    Rows find(std::string_view coll, const Row& filter, int limit, int skip,
              std::string_view sort_field, bool sort_asc) override {
        std::lock_guard lk(mu_);
        Rows out;
        auto it = colls_.find(std::string(coll));
        if (it == colls_.end()) return out;
        int skipped = 0;
        for (auto& d : it->second) {
            if (!match_filter_(d, filter)) continue;
            if (skipped < skip) {
                ++skipped;
                continue;
            }
            out.push_back(d);
            if (limit > 0 && static_cast<int>(out.size()) >= limit) break;
        }
        if (!sort_field.empty()) {
            std::string sf(sort_field);
            std::sort(out.begin(), out.end(), [&](const Row& a, const Row& b) {
                auto av = a.contains(sf) ? a.at(sf) : nlohmann::json{};
                auto bv = b.contains(sf) ? b.at(sf) : nlohmann::json{};
                return sort_asc ? av < bv : bv < av;
            });
        }
        return out;
    }

    std::int64_t update_many(std::string_view coll, const Row& filter,
                             const Row& set_fields) override {
        std::lock_guard lk(mu_);
        auto it = colls_.find(std::string(coll));
        if (it == colls_.end()) return 0;
        std::int64_t n = 0;
        for (auto& d : it->second) {
            if (!match_filter_(d, filter)) continue;
            for (auto s = set_fields.begin(); s != set_fields.end(); ++s) d[s.key()] = s.value();
            ++n;
        }
        return n;
    }

    std::int64_t delete_many(std::string_view coll, const Row& filter) override {
        std::lock_guard lk(mu_);
        auto it = colls_.find(std::string(coll));
        if (it == colls_.end()) return 0;
        auto& c = it->second;
        std::int64_t before = static_cast<std::int64_t>(c.size());
        c.erase(std::remove_if(c.begin(), c.end(),
                               [&](const Row& d) { return match_filter_(d, filter); }),
                c.end());
        return before - static_cast<std::int64_t>(c.size());
    }

    std::int64_t count(std::string_view coll, const Row& filter) override {
        return static_cast<std::int64_t>(find(coll, filter, 0, 0, {}, true).size());
    }

    void create_index(std::string_view, const Row&, bool) override {
        // memory engine: uniqueness checked at insert time optionally — no-op indexes
    }

    void drop_collection(std::string_view coll) override {
        std::lock_guard lk(mu_);
        colls_.erase(std::string(coll));
    }

private:
    std::mutex mu_;
    std::unordered_map<std::string, Rows> colls_;
    std::int64_t seq_{0};
};

#if SOCKETIFY_HAS_MONGO

mongocxx::instance& mongo_instance_() {
    static mongocxx::instance inst{};
    return inst;
}

class MongoEngine final : public DocumentEngine {
public:
    explicit MongoEngine(const Mongo& opts) : opts_(opts) {
        mongo_instance_();
        client_ = mongocxx::client{mongocxx::uri{opts.uri}};
        db_ = client_[opts.db];
    }

    void insert_one(std::string_view coll, const Row& doc) override {
        auto view = bsoncxx::from_json(doc.dump());
        db_[std::string(coll)].insert_one(view.view());
    }

    std::optional<Row> find_one(std::string_view coll, const Row& filter) override {
        auto f = bsoncxx::from_json(filter.dump());
        auto r = db_[std::string(coll)].find_one(f.view());
        if (!r) return std::nullopt;
        return nlohmann::json::parse(bsoncxx::to_json(*r));
    }

    Rows find(std::string_view coll, const Row& filter, int limit, int skip,
              std::string_view sort_field, bool sort_asc) override {
        mongocxx::options::find opts;
        if (limit > 0) opts.limit(limit);
        if (skip > 0) opts.skip(skip);
        if (!sort_field.empty()) {
            bsoncxx::builder::basic::document s;
            s.append(bsoncxx::builder::basic::kvp(std::string(sort_field), sort_asc ? 1 : -1));
            opts.sort(s.view());
        }
        auto f = bsoncxx::from_json(filter.dump());
        Rows out;
        auto cursor = db_[std::string(coll)].find(f.view(), opts);
        for (auto&& doc : cursor) {
            out.push_back(nlohmann::json::parse(bsoncxx::to_json(doc)));
        }
        return out;
    }

    std::int64_t update_many(std::string_view coll, const Row& filter,
                             const Row& set_fields) override {
        auto f = bsoncxx::from_json(filter.dump());
        nlohmann::json setdoc = {{"$set", set_fields}};
        auto u = bsoncxx::from_json(setdoc.dump());
        auto r = db_[std::string(coll)].update_many(f.view(), u.view());
        return static_cast<std::int64_t>(r->modified_count());
    }

    std::int64_t delete_many(std::string_view coll, const Row& filter) override {
        auto f = bsoncxx::from_json(filter.dump());
        auto r = db_[std::string(coll)].delete_many(f.view());
        return static_cast<std::int64_t>(r->deleted_count());
    }

    std::int64_t count(std::string_view coll, const Row& filter) override {
        auto f = bsoncxx::from_json(filter.dump());
        return static_cast<std::int64_t>(db_[std::string(coll)].count_documents(f.view()));
    }

    void create_index(std::string_view coll, const Row& keys, bool unique) override {
        auto k = bsoncxx::from_json(keys.dump());
        mongocxx::options::index opts;
        opts.unique(unique);
        db_[std::string(coll)].create_index(k.view(), opts);
    }

    void drop_collection(std::string_view coll) override {
        db_[std::string(coll)].drop();
    }

private:
    Mongo opts_;
    mongocxx::client client_;
    mongocxx::database db_;
};

#endif

} // namespace

DocumentEnginePtr make_mongo_engine(const Mongo& opts) {
    if (opts.uri == "memory://" || opts.uri.rfind("memory://", 0) == 0) {
        return std::make_shared<MemoryDocumentEngine>();
    }
#if SOCKETIFY_HAS_MONGO
    return std::make_shared<MongoEngine>(opts);
#else
    throw Error(
        "Mongo URI requires SOCKETIFY_WITH_MONGO=ON, or use Mongo{.uri=\"memory://\"} for "
        "in-process documents");
#endif
}

} // namespace socketify::db
