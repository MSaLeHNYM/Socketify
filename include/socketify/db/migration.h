#pragma once
/**
 * @file migration.h
 * @brief Versioned SQL migrations registry.
 */

#include "socketify/db/schema.h"

#include <functional>
#include <string>
#include <vector>

namespace socketify::db {

class Database;

struct Migration {
    std::string version; // e.g. "202601010000_create_users"
    std::function<void(Database&)> up;
    std::function<void(Database&)> down;
};

class MigrationRegistry {
public:
    static MigrationRegistry& instance();
    void add(Migration m);
    const std::vector<Migration>& all() const noexcept { return items_; }

private:
    std::vector<Migration> items_;
};

/** @brief Register a migration at static init time. */
struct MigrationRegistrar {
    explicit MigrationRegistrar(Migration m) {
        MigrationRegistry::instance().add(std::move(m));
    }
};

#define SOCKETIFY_MIGRATE(version_str, up_body, down_body)                     \
    static ::socketify::db::MigrationRegistrar                                 \
        SOCKETIFY_MIG_##version_str{::socketify::db::Migration{                \
            #version_str,                                                      \
            [](::socketify::db::Database& db) up_body,                          \
            [](::socketify::db::Database& db) down_body}};

} // namespace socketify::db
