/**
 * @file sqlite_engine.cpp
 * @brief SQLite SqlEngine implementation.
 */

#include "socketify/db/database.h"
#include "socketify/db/error.h"

#if SOCKETIFY_HAS_SQLITE
#include <sqlite3.h>
#include <cstdint>
#include <cstring>
#include <mutex>
#endif

namespace socketify::db {

#if SOCKETIFY_HAS_SQLITE
namespace {

class SqliteEngine final : public SqlEngine {
public:
    explicit SqliteEngine(const std::string& path) {
        if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
            std::string msg = db_ ? sqlite3_errmsg(db_) : "sqlite open failed";
            if (db_) sqlite3_close(db_);
            throw Error(msg);
        }
        sqlite3_exec(db_, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);
        sqlite3_busy_timeout(db_, 5000);
    }

    ~SqliteEngine() override {
        if (db_) sqlite3_close(db_);
    }

    Dialect dialect() const override { return Dialect::Sqlite; }

    void exec(std::string_view sql, const Params& params) override {
        std::lock_guard lk(mu_);
        sqlite3_stmt* st = prepare_(sql);
        bind_(st, params);
        int rc = sqlite3_step(st);
        sqlite3_finalize(st);
        if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
            throw Error(sqlite3_errmsg(db_), sqlite3_errcode(db_));
        }
    }

    Rows query(std::string_view sql, const Params& params) override {
        std::lock_guard lk(mu_);
        sqlite3_stmt* st = prepare_(sql);
        bind_(st, params);
        Rows rows;
        int cols = sqlite3_column_count(st);
        while (true) {
            int rc = sqlite3_step(st);
            if (rc == SQLITE_DONE) break;
            if (rc != SQLITE_ROW) {
                sqlite3_finalize(st);
                throw Error(sqlite3_errmsg(db_), sqlite3_errcode(db_));
            }
            Row row = Row::object();
            for (int i = 0; i < cols; ++i) {
                const char* name = sqlite3_column_name(st, i);
                switch (sqlite3_column_type(st, i)) {
                    case SQLITE_INTEGER:
                        row[name] = static_cast<std::int64_t>(sqlite3_column_int64(st, i));
                        break;
                    case SQLITE_FLOAT:
                        row[name] = sqlite3_column_double(st, i);
                        break;
                    case SQLITE_TEXT: {
                        auto* p = reinterpret_cast<const char*>(sqlite3_column_text(st, i));
                        row[name] = p ? std::string(p) : std::string{};
                        break;
                    }
                    case SQLITE_BLOB: {
                        auto* p = static_cast<const unsigned char*>(sqlite3_column_blob(st, i));
                        int n = sqlite3_column_bytes(st, i);
                        row[name] = std::vector<std::uint8_t>(p, p + n);
                        break;
                    }
                    default:
                        row[name] = nullptr;
                        break;
                }
            }
            rows.push_back(std::move(row));
        }
        sqlite3_finalize(st);
        return rows;
    }

    std::int64_t insert(std::string_view sql, const Params& params) override {
        exec(sql, params);
        std::lock_guard lk(mu_);
        return static_cast<std::int64_t>(sqlite3_last_insert_rowid(db_));
    }

    void begin() override { exec("BEGIN", {}); }
    void commit() override { exec("COMMIT", {}); }
    void rollback() override { exec("ROLLBACK", {}); }

private:
    sqlite3* db_{nullptr};
    std::mutex mu_;

    sqlite3_stmt* prepare_(std::string_view sql) {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &st, nullptr) !=
            SQLITE_OK) {
            throw Error(sqlite3_errmsg(db_), sqlite3_errcode(db_));
        }
        return st;
    }

    void bind_(sqlite3_stmt* st, const Params& params) {
        for (int i = 0; i < static_cast<int>(params.size()); ++i) {
            const auto& v = params[static_cast<std::size_t>(i)];
            int idx = i + 1;
            if (v.is_null()) {
                sqlite3_bind_null(st, idx);
            } else if (v.is_boolean()) {
                sqlite3_bind_int(st, idx, v.get<bool>() ? 1 : 0);
            } else if (v.is_number_integer()) {
                sqlite3_bind_int64(st, idx, v.get<std::int64_t>());
            } else if (v.is_number_float()) {
                sqlite3_bind_double(st, idx, v.get<double>());
            } else if (v.is_string()) {
                const auto& s = v.get_ref<const std::string&>();
                // SQLITE_TRANSIENT uses an old-style cast in sqlite3.h — avoid -Wold-style-cast.
                auto* dtor = reinterpret_cast<sqlite3_destructor_type>(
                    static_cast<std::intptr_t>(-1));
                sqlite3_bind_text(st, idx, s.data(), static_cast<int>(s.size()), dtor);
            } else {
                auto s = v.dump();
                auto* dtor = reinterpret_cast<sqlite3_destructor_type>(
                    static_cast<std::intptr_t>(-1));
                sqlite3_bind_text(st, idx, s.data(), static_cast<int>(s.size()), dtor);
            }
        }
    }
};

} // namespace

SqlEnginePtr make_sqlite_engine(const Sqlite& opts) {
    return std::make_shared<SqliteEngine>(opts.path);
}

#else

SqlEnginePtr make_sqlite_engine(const Sqlite&) {
    throw Error("Socketify built without SQLite support (SOCKETIFY_WITH_SQLITE=ON)");
}

#endif

} // namespace socketify::db
