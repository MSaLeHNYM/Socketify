/**
 * @file record.cpp
 * @brief Record update/save/destroy/related helpers.
 */

#include "socketify/db/model.h"
#include "socketify/db/query.h"
#include "socketify/db/schema.h"

namespace socketify::db {

void Record::update(const Row& patch) {
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        attrs_[it.key()] = it.value();
        dirty_keys_.insert(it.key());
    }
    dirty_ = true;
    save();
}

bool Record::save() {
    if (!db_) throw Error("record has no database");
    auto idv = attrs_.contains("id") ? attrs_["id"] : nlohmann::json{};
    if (idv.is_null() || (idv.is_number() && idv.get<std::int64_t>() == 0)) {
        throw Error("cannot save record without id — use Model::create");
    }
    Row patch = Row::object();
    for (auto it = attrs_.begin(); it != attrs_.end(); ++it) {
        if (it.key() != "id") patch[it.key()] = it.value();
    }
    persist_update_(patch);
    dirty_ = false;
    dirty_keys_.clear();
    return true;
}

bool Record::destroy() {
    if (!db_) throw Error("record has no database");
    persist_destroy_();
    return true;
}

std::int64_t Record::persist_update_(const Row& patch) {
    return Query(db_, table_).where_eq("id", id()).update(patch);
}

std::int64_t Record::persist_destroy_() {
    return Query(db_, table_).where_eq("id", id()).destroy();
}

Rows Record::related(std::string_view name) {
    throw Error(std::string("unknown relation: ") + std::string(name) +
                " — use Model::related after defining associations in boot()");
}

} // namespace socketify::db
