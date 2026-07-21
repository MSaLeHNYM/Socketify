/**
 * @file postgres_engine.cpp
 * @brief PostgreSQL SqlEngine (libpq). Built when SOCKETIFY_HAS_POSTGRES=1.
 */

#include "socketify/db/database.h"
#include "socketify/db/error.h"

#if SOCKETIFY_HAS_POSTGRES
#include <libpq-fe.h>
#include <cstdlib>
#include <sstream>
#endif

namespace socketify::db {

#if SOCKETIFY_HAS_POSTGRES
namespace {

std::string rewrite_placeholders_(std::string_view sql, int nparams) {
    // Convert ? to $1,$2,...
    std::string out;
    out.reserve(sql.size() + static_cast<std::size_t>(nparams) * 2);
    int idx = 0;
    for (char c : sql) {
        if (c == '?') {
            ++idx;
            out.push_back('$');
            out += std::to_string(idx);
        } else {
            out.push_back(c);
        }
    }
    (void)nparams;
    return out;
}

class PostgresEngine final : public SqlEngine {
public:
    explicit PostgresEngine(const Postgres& opts) {
        std::ostringstream cs;
        cs << "host=" << opts.host << " port=" << opts.port << " dbname=" << opts.db
           << " user=" << opts.user;
        if (!opts.password.empty()) cs << " password=" << opts.password;
        conn_ = PQconnectdb(cs.str().c_str());
        if (PQstatus(conn_) != CONNECTION_OK) {
            std::string msg = PQerrorMessage(conn_);
            PQfinish(conn_);
            throw Error(msg);
        }
    }

    ~PostgresEngine() override {
        if (conn_) PQfinish(conn_);
    }

    Dialect dialect() const override { return Dialect::Postgres; }

    std::int64_t exec(std::string_view sql, const Params& params) override {
        auto r = exec_params_(sql, params);
        auto status = PQresultStatus(r);
        if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
            std::string msg = PQerrorMessage(conn_);
            PQclear(r);
            throw Error(msg);
        }
        const char* tuples = PQcmdTuples(r);
        std::int64_t n = (tuples && tuples[0]) ? std::atoll(tuples) : 0;
        PQclear(r);
        return n;
    }

    Rows query(std::string_view sql, const Params& params) override {
        auto r = exec_params_(sql, params);
        if (PQresultStatus(r) != PGRES_TUPLES_OK) {
            std::string msg = PQerrorMessage(conn_);
            PQclear(r);
            throw Error(msg);
        }
        Rows rows;
        int nrows = PQntuples(r);
        int ncols = PQnfields(r);
        for (int i = 0; i < nrows; ++i) {
            Row row = Row::object();
            for (int c = 0; c < ncols; ++c) {
                const char* name = PQfname(r, c);
                if (PQgetisnull(r, i, c)) {
                    row[name] = nullptr;
                } else {
                    row[name] = std::string(PQgetvalue(r, i, c));
                }
            }
            rows.push_back(std::move(row));
        }
        PQclear(r);
        return rows;
    }

    std::int64_t insert(std::string_view sql, const Params& params) override {
        std::string s(sql);
        // Prefer RETURNING id when absent
        if (s.find("RETURNING") == std::string::npos &&
            s.find("returning") == std::string::npos) {
            s += " RETURNING id";
        }
        auto rows = query(s, params);
        if (!rows.empty() && rows[0].contains("id")) {
            if (rows[0]["id"].is_number()) return rows[0]["id"].get<std::int64_t>();
            return std::stoll(rows[0]["id"].get<std::string>());
        }
        return 0;
    }

    void begin() override { (void)exec("BEGIN", {}); }
    void commit() override { (void)exec("COMMIT", {}); }
    void rollback() override { (void)exec("ROLLBACK", {}); }

private:
    PGconn* conn_{nullptr};

    PGresult* exec_params_(std::string_view sql, const Params& params) {
        auto rewritten = rewrite_placeholders_(sql, static_cast<int>(params.size()));
        std::vector<std::string> storage;
        storage.reserve(params.size());
        std::vector<const char*> values;
        values.reserve(params.size());
        for (const auto& p : params) {
            if (p.is_null()) {
                values.push_back(nullptr);
            } else if (p.is_string()) {
                storage.push_back(p.get<std::string>());
                values.push_back(storage.back().c_str());
            } else if (p.is_boolean()) {
                storage.push_back(p.get<bool>() ? "t" : "f");
                values.push_back(storage.back().c_str());
            } else {
                storage.push_back(p.dump());
                // dump adds quotes for strings already handled; numbers fine
                if (p.is_number()) storage.back() = p.is_number_float()
                                                        ? std::to_string(p.get<double>())
                                                        : std::to_string(p.get<std::int64_t>());
                values.push_back(storage.back().c_str());
            }
        }
        return PQexecParams(conn_, rewritten.c_str(), static_cast<int>(values.size()), nullptr,
                            values.data(), nullptr, nullptr, 0);
    }
};

} // namespace

SqlEnginePtr make_postgres_engine(const Postgres& opts) {
    return std::make_shared<PostgresEngine>(opts);
}

#else

SqlEnginePtr make_postgres_engine(const Postgres&) {
    throw Error("Socketify built without PostgreSQL support (SOCKETIFY_WITH_POSTGRES=ON)");
}

#endif

} // namespace socketify::db
