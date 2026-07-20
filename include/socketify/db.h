#pragma once
/**
 * @file db.h
 * @brief Umbrella header for socketify::db ORM (SQL + Mongo).
 *
 * @code
 * #include <socketify/db.h>
 * using namespace socketify::db;
 *
 * struct User : Model<User> {
 *     static constexpr std::string_view table = "users";
 *     static Schema schema() {
 *         return Schema::create(table)
 *             .integer("id").primary().autoincrement()
 *             .text("email").unique().not_null()
 *             .text("name").not_null()
 *             .timestamps();
 *     }
 *     static void boot() {
 *         validates("email", required(), email());
 *     }
 * };
 *
 * auto db = Database::open(Sqlite{.path = "app.db"});
 * User::migrate_schema(db);
 * auto u = User::create(db, {{"email","a@b.c"},{"name","Ada"}});
 * @endcode
 */

#include "socketify/db/database.h"
#include "socketify/db/document.h"
#include "socketify/db/error.h"
#include "socketify/db/migration.h"
#include "socketify/db/model.h"
#include "socketify/db/query.h"
#include "socketify/db/relation.h"
#include "socketify/db/schema.h"
#include "socketify/db/validation.h"
