#pragma once
/**
 * @file relation.h
 * @brief Relation metadata for SQL models.
 */

#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

namespace socketify::db {

enum class RelationKind : std::uint8_t {
    HasMany,
    BelongsTo,
    HasOne,
    BelongsToMany,
};

struct RelationMeta {
    RelationKind kind;
    std::string name;          // association name
    std::string related_table;
    std::string foreign_key;   // on related (has_many) or self (belongs_to)
    std::string local_key{"id"};
    std::string pivot_table;   // belongs_to_many
    std::string related_key{"id"};
};

/** @brief Per-model relation registry (populated in Model::boot). */
class RelationRegistry {
public:
    void add(RelationMeta m) { items_.push_back(std::move(m)); }
    const std::vector<RelationMeta>& all() const noexcept { return items_; }
    const RelationMeta* find(std::string_view name) const {
        for (auto& r : items_)
            if (r.name == name) return &r;
        return nullptr;
    }

private:
    std::vector<RelationMeta> items_;
};

} // namespace socketify::db
