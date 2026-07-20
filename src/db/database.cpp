/**
 * @file database.cpp
 * @brief Database facade implementation.
 */

#include "socketify/db/database.h"
#include "socketify/db/schema.h"

#include <unordered_map>

namespace socketify::db {
namespace {

thread_local SqlEnginePtr tls_engine;

} // namespace

Database::TransactionScope::TransactionScope(Database& d, SqlEnginePtr eng)
    : db(d), prev(tls_engine) {
    tls_engine = std::move(eng);
    (void)db;
}

Database::TransactionScope::~TransactionScope() { tls_engine = prev; }

Database Database::open(const Sqlite& opts) {
    Database db;
    db.dialect_ = Dialect::Sqlite;
    auto factory = [opts] { return make_sqlite_engine(opts); };
    db.pool_ = std::make_shared<Pool>(factory, opts.pool_size);
    return db;
}

Database Database::open(const Postgres& opts) {
    Database db;
    db.dialect_ = Dialect::Postgres;
    auto factory = [opts] { return make_postgres_engine(opts); };
    db.pool_ = std::make_shared<Pool>(factory, opts.pool_size);
    return db;
}

Database Database::open(const Mysql& opts) {
    Database db;
    db.dialect_ = Dialect::Mysql;
    auto factory = [opts] { return make_mysql_engine(opts); };
    db.pool_ = std::make_shared<Pool>(factory, opts.pool_size);
    return db;
}

Database Database::open(const Mongo& opts) {
    Database db;
    db.doc_ = make_mongo_engine(opts);
    return db;
}

void Database::exec(std::string_view sql, const Params& params) {
    ensure_sql_();
    if (tls_engine) {
        tls_engine->exec(sql, params);
        return;
    }
    auto lease = pool_->acquire();
    lease->exec(sql, params);
}

Rows Database::query(std::string_view sql, const Params& params) {
    ensure_sql_();
    if (tls_engine) return tls_engine->query(sql, params);
    auto lease = pool_->acquire();
    return lease->query(sql, params);
}

std::int64_t Database::insert(std::string_view sql, const Params& params) {
    ensure_sql_();
    if (tls_engine) return tls_engine->insert(sql, params);
    auto lease = pool_->acquire();
    return lease->insert(sql, params);
}

void Database::create_table(const Schema& schema) {
    auto sql = schema.to_sql(dialect_);
    std::size_t start = 0;
    while (start < sql.size()) {
        auto pos = sql.find(';', start);
        if (pos == std::string::npos) break;
        auto stmt = sql.substr(start, pos - start);
        while (!stmt.empty() && (stmt.front() == ' ' || stmt.front() == '\n' || stmt.front() == '\r'))
            stmt.erase(stmt.begin());
        if (!stmt.empty()) exec(stmt);
        start = pos + 1;
    }
}

void Database::drop_table(std::string_view table) {
    exec("DROP TABLE IF EXISTS " + quote_ident(dialect_, table));
}

void Database::migrate() {
    ensure_sql_();
    exec("CREATE TABLE IF NOT EXISTS " + quote_ident(dialect_, "_socketify_migrations") + " (" +
         quote_ident(dialect_, "version") + " TEXT PRIMARY KEY, " +
         quote_ident(dialect_, "applied_at") + " TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)");

    auto applied =
        query("SELECT " + quote_ident(dialect_, "version") + " FROM " +
              quote_ident(dialect_, "_socketify_migrations"));
    std::unordered_map<std::string, bool> have;
    for (auto& r : applied) have[r["version"].get<std::string>()] = true;

    for (const auto& m : MigrationRegistry::instance().all()) {
        if (have[m.version]) continue;
        transaction([&] {
            m.up(*this);
            exec("INSERT INTO " + quote_ident(dialect_, "_socketify_migrations") + " (" +
                     quote_ident(dialect_, "version") + ") VALUES (?)",
                 {m.version});
            return 0;
        });
    }
}

void Database::rollback_last() {
    ensure_sql_();
    auto rows = query("SELECT " + quote_ident(dialect_, "version") + " FROM " +
                      quote_ident(dialect_, "_socketify_migrations") + " ORDER BY " +
                      quote_ident(dialect_, "applied_at") + " DESC LIMIT 1");
    if (rows.empty()) return;
    auto ver = rows[0]["version"].get<std::string>();
    for (const auto& m : MigrationRegistry::instance().all()) {
        if (m.version != ver) continue;
        transaction([&] {
            if (m.down) m.down(*this);
            exec("DELETE FROM " + quote_ident(dialect_, "_socketify_migrations") + " WHERE " +
                     quote_ident(dialect_, "version") + " = ?",
                 {ver});
            return 0;
        });
        return;
    }
}

} // namespace socketify::db
