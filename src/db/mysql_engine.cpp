/**
 * @file mysql_engine.cpp
 * @brief MySQL SqlEngine (libmysqlclient). Built when SOCKETIFY_HAS_MYSQL=1.
 */

#include "socketify/db/database.h"
#include "socketify/db/error.h"

#if SOCKETIFY_HAS_MYSQL
#include <mysql/mysql.h>
#include <cstring>
#endif

namespace socketify::db {

#if SOCKETIFY_HAS_MYSQL
namespace {

class MysqlEngine final : public SqlEngine {
public:
    explicit MysqlEngine(const Mysql& opts) {
        conn_ = mysql_init(nullptr);
        if (!conn_) throw Error("mysql_init failed");
        if (!mysql_real_connect(conn_, opts.host.c_str(), opts.user.c_str(), opts.password.c_str(),
                                opts.db.c_str(), opts.port, nullptr, 0)) {
            std::string msg = mysql_error(conn_);
            mysql_close(conn_);
            throw Error(msg);
        }
    }

    ~MysqlEngine() override {
        if (conn_) mysql_close(conn_);
    }

    Dialect dialect() const override { return Dialect::Mysql; }

    void exec(std::string_view sql, const Params& params) override {
        auto bound = bind_sql_(sql, params);
        if (mysql_real_query(conn_, bound.data(), bound.size()) != 0) {
            throw Error(mysql_error(conn_), static_cast<int>(mysql_errno(conn_)));
        }
        // drain result if any
        if (auto* res = mysql_store_result(conn_)) mysql_free_result(res);
    }

    Rows query(std::string_view sql, const Params& params) override {
        auto bound = bind_sql_(sql, params);
        if (mysql_real_query(conn_, bound.data(), bound.size()) != 0) {
            throw Error(mysql_error(conn_), static_cast<int>(mysql_errno(conn_)));
        }
        MYSQL_RES* res = mysql_store_result(conn_);
        if (!res) {
            if (mysql_field_count(conn_) == 0) return {};
            throw Error(mysql_error(conn_), static_cast<int>(mysql_errno(conn_)));
        }
        Rows rows;
        unsigned cols = mysql_num_fields(res);
        MYSQL_FIELD* fields = mysql_fetch_fields(res);
        while (MYSQL_ROW row = mysql_fetch_row(res)) {
            auto* lengths = mysql_fetch_lengths(res);
            Row obj = Row::object();
            for (unsigned i = 0; i < cols; ++i) {
                if (!row[i]) obj[fields[i].name] = nullptr;
                else obj[fields[i].name] = std::string(row[i], lengths[i]);
            }
            rows.push_back(std::move(obj));
        }
        mysql_free_result(res);
        return rows;
    }

    std::int64_t insert(std::string_view sql, const Params& params) override {
        exec(sql, params);
        return static_cast<std::int64_t>(mysql_insert_id(conn_));
    }

    void begin() override { exec("START TRANSACTION", {}); }
    void commit() override { exec("COMMIT", {}); }
    void rollback() override { exec("ROLLBACK", {}); }

private:
    MYSQL* conn_{nullptr};

    std::string escape_(const std::string& in) {
        std::string out(in.size() * 2 + 1, '\0');
        auto n = mysql_real_escape_string(conn_, out.data(), in.data(), in.size());
        out.resize(n);
        return out;
    }

    std::string bind_sql_(std::string_view sql, const Params& params) {
        std::string out;
        out.reserve(sql.size() + 16);
        std::size_t pi = 0;
        for (char c : sql) {
            if (c != '?') {
                out.push_back(c);
                continue;
            }
            if (pi >= params.size()) throw Error("not enough bind parameters");
            const auto& v = params[pi++];
            if (v.is_null()) out += "NULL";
            else if (v.is_boolean()) out += v.get<bool>() ? "1" : "0";
            else if (v.is_number_integer()) out += std::to_string(v.get<std::int64_t>());
            else if (v.is_number_float()) out += std::to_string(v.get<double>());
            else if (v.is_string()) {
                out.push_back('\'');
                out += escape_(v.get<std::string>());
                out.push_back('\'');
            } else {
                out.push_back('\'');
                out += escape_(v.dump());
                out.push_back('\'');
            }
        }
        return out;
    }
};

} // namespace

SqlEnginePtr make_mysql_engine(const Mysql& opts) {
    return std::make_shared<MysqlEngine>(opts);
}

#else

SqlEnginePtr make_mysql_engine(const Mysql&) {
    throw Error("Socketify built without MySQL support (SOCKETIFY_WITH_MYSQL=ON)");
}

#endif

} // namespace socketify::db
