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
    Row patch = attrs_;
    patch.erase("id");
    Query(db_, table_).where_eq("id", id()).update(patch);
    dirty_ = false;
    return true;
}

bool Record::destroy() {
    if (!db_) throw Error("record has no database");
    Query(db_, table_).where_eq("id", id()).destroy();
    return true;
}

Rows Record::related(std::string_view name) {
    // Relations live on Model meta; Record only has table. Look up via a side channel:
    // Callers using Model::related go through Model which has meta. For Record::related
    // we need the registry — Model overrides and passes meta.
    throw Error(std::string("unknown relation: ") + std::string(name) +
                " — use Model::related after defining associations in boot()");
}

} // namespace socketify::db
