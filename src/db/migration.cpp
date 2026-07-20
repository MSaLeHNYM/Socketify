/**
 * @file migration.cpp
 * @brief Global migration registry.
 */

#include "socketify/db/migration.h"

namespace socketify::db {

MigrationRegistry& MigrationRegistry::instance() {
    static MigrationRegistry reg;
    return reg;
}

void MigrationRegistry::add(Migration m) { items_.push_back(std::move(m)); }

} // namespace socketify::db
