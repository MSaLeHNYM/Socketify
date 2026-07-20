#pragma once
/**
 * @file database.h
 * @brief Database facade: connect, pool, migrate, raw SQL, transactions.
 */

#include "socketify/db/engine.h"
#include "socketify/db/error.h"
#include "socketify/db/migration.h"
#include "socketify/db/pool.h"
#include "socketify/db/schema.h"

#include <memory>
#include <optional>
#include <string>
#include <variant>

namespace socketify::db {

struct Sqlite {
    std::string path{":memory:"};
    std::size_t pool_size{4};
};

struct Postgres {
    std::string host{"127.0.0.1"};
    std::uint16_t port{5432};
    std::string db{"postgres"};
    std::string user{"postgres"};
    std::string password{};
    std::size_t pool_size{4};
};

struct Mysql {
    std::string host{"127.0.0.1"};
    std::uint16_t port{3306};
    std::string db{"mysql"};
    std::string user{"root"};
    std::string password{};
    std::size_t pool_size{4};
};

struct Mongo {
    std::string uri{"mongodb://127.0.0.1:27017"};
    std::string db{"test"};
};

class Database {
public:
    static Database open(const Sqlite& opts);
    static Database open(const Postgres& opts);
    static Database open(const Mysql& opts);
    static Database open(const Mongo& opts);

    Database() = default;

    bool is_sql() const noexcept { return static_cast<bool>(pool_); }
    bool is_document() const noexcept { return static_cast<bool>(doc_); }

    Dialect dialect() const noexcept { return dialect_; }

    void exec(std::string_view sql, const Params& params = {});
    Rows query(std::string_view sql, const Params& params = {});
    std::int64_t insert(std::string_view sql, const Params& params = {});

    void create_table(const Schema& schema);
    void drop_table(std::string_view table);

    void migrate();
    void rollback_last();

    template <typename Fn>
    auto transaction(Fn&& fn) -> decltype(fn()) {
        ensure_sql_();
        auto lease = pool_->acquire();
        lease->begin();
        try {
            TransactionScope scope(*this, lease.get());
            auto result = fn();
            lease->commit();
            return result;
        } catch (...) {
            try {
                lease->rollback();
            } catch (...) {
            }
            throw;
        }
    }

    Pool::Lease acquire() {
        ensure_sql_();
        return pool_->acquire();
    }

    DocumentEngine& documents() {
        if (!doc_) throw Error("not a document database");
        return *doc_;
    }

    DocumentEnginePtr document_engine() const { return doc_; }
    std::shared_ptr<Pool> pool() const { return pool_; }

private:
    friend class Query;
    struct TransactionScope {
        Database& db;
        SqlEnginePtr prev;
        explicit TransactionScope(Database& d, SqlEnginePtr eng);
        ~TransactionScope();
    };

    std::shared_ptr<Pool> pool_;
    DocumentEnginePtr doc_;
    Dialect dialect_{Dialect::Sqlite};

    void ensure_sql_() const {
        if (!pool_) throw Error("not a SQL database");
    }
};

SqlEnginePtr make_sqlite_engine(const Sqlite& opts);
SqlEnginePtr make_postgres_engine(const Postgres& opts);
SqlEnginePtr make_mysql_engine(const Mysql& opts);
DocumentEnginePtr make_mongo_engine(const Mongo& opts);

} // namespace socketify::db
