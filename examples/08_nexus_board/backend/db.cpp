#include "db.h"

#include <socketify/detail/utils.h>

#include "../../third_party/sqlite/sqlite3.h"

#include <stdexcept>
#include <cstring>

namespace nexus {
using nlohmann::json;
using socketify::detail::hex_encode;
using socketify::detail::random_token;
using socketify::detail::sha256;

namespace {

void bind_text(sqlite3_stmt* st, int i, std::string_view s) {
    sqlite3_bind_text(st, i, s.data(), static_cast<int>(s.size()), SQLITE_TRANSIENT);
}

std::string col_text(sqlite3_stmt* st, int i) {
    auto* p = reinterpret_cast<const char*>(sqlite3_column_text(st, i));
    return p ? std::string(p) : std::string{};
}

} // namespace

Database::Database(std::string path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        throw std::runtime_error(std::string("sqlite open: ") + sqlite3_errmsg(db_));
    }
    sqlite3_exec(db_, "PRAGMA foreign_keys = ON; PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);
}

Database::~Database() {
    if (db_) sqlite3_close(db_);
}

void Database::exec_(std::string_view sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, std::string(sql).c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown";
        sqlite3_free(err);
        throw std::runtime_error("sqlite: " + msg);
    }
}

std::string Database::make_salt_() { return random_token(16); }

std::string Database::hash_password_(std::string_view salt, std::string_view password) {
    auto dig = sha256(std::string(salt) + ":" + std::string(password));
    return hex_encode(dig.data(), dig.size());
}

void Database::migrate() {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    exec_(R"SQL(
CREATE TABLE IF NOT EXISTS users (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  email TEXT NOT NULL UNIQUE COLLATE NOCASE,
  name TEXT NOT NULL,
  pass_hash TEXT NOT NULL,
  salt TEXT NOT NULL,
  avatar_path TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL DEFAULT (datetime('now'))
);
CREATE TABLE IF NOT EXISTS projects (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL,
  description TEXT NOT NULL DEFAULT '',
  owner_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  created_at TEXT NOT NULL DEFAULT (datetime('now'))
);
CREATE TABLE IF NOT EXISTS project_members (
  project_id INTEGER NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
  user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  role TEXT NOT NULL DEFAULT 'member',
  PRIMARY KEY (project_id, user_id)
);
CREATE TABLE IF NOT EXISTS tasks (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  project_id INTEGER NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
  title TEXT NOT NULL,
  body TEXT NOT NULL DEFAULT '',
  status TEXT NOT NULL DEFAULT 'todo',
  priority INTEGER NOT NULL DEFAULT 1,
  assignee_id INTEGER REFERENCES users(id) ON DELETE SET NULL,
  creator_id INTEGER NOT NULL REFERENCES users(id),
  created_at TEXT NOT NULL DEFAULT (datetime('now')),
  updated_at TEXT NOT NULL DEFAULT (datetime('now'))
);
CREATE TABLE IF NOT EXISTS comments (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  task_id INTEGER NOT NULL REFERENCES tasks(id) ON DELETE CASCADE,
  user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  body TEXT NOT NULL,
  created_at TEXT NOT NULL DEFAULT (datetime('now'))
);
CREATE INDEX IF NOT EXISTS idx_tasks_project ON tasks(project_id);
CREATE INDEX IF NOT EXISTS idx_comments_task ON comments(task_id);
)SQL");
}

json Database::user_row_(sqlite3_stmt* st) {
    return json{
        {"id", sqlite3_column_int64(st, 0)},
        {"email", col_text(st, 1)},
        {"name", col_text(st, 2)},
        {"avatar_path", col_text(st, 3)},
        {"created_at", col_text(st, 4)},
    };
}

json Database::project_row_(sqlite3_stmt* st) {
    return json{
        {"id", sqlite3_column_int64(st, 0)},
        {"name", col_text(st, 1)},
        {"description", col_text(st, 2)},
        {"owner_id", sqlite3_column_int64(st, 3)},
        {"created_at", col_text(st, 4)},
    };
}

json Database::task_row_(sqlite3_stmt* st) {
    json j{
        {"id", sqlite3_column_int64(st, 0)},
        {"project_id", sqlite3_column_int64(st, 1)},
        {"title", col_text(st, 2)},
        {"body", col_text(st, 3)},
        {"status", col_text(st, 4)},
        {"priority", sqlite3_column_int(st, 5)},
        {"creator_id", sqlite3_column_int64(st, 7)},
        {"created_at", col_text(st, 8)},
        {"updated_at", col_text(st, 9)},
    };
    if (sqlite3_column_type(st, 6) == SQLITE_NULL) j["assignee_id"] = nullptr;
    else j["assignee_id"] = sqlite3_column_int64(st, 6);
    return j;
}

std::optional<json> Database::create_user(std::string_view email, std::string_view name,
                                          std::string_view password) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    std::string salt = make_salt_();
    std::string hash = hash_password_(salt, password);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "INSERT INTO users(email,name,pass_hash,salt) VALUES(?,?,?,?)",
        -1, &st, nullptr);
    bind_text(st, 1, email);
    bind_text(st, 2, name);
    bind_text(st, 3, hash);
    bind_text(st, 4, salt);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return std::nullopt;
    return user_by_id(sqlite3_last_insert_rowid(db_));
}

std::optional<json> Database::authenticate(std::string_view email, std::string_view password) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT id,email,name,avatar_path,created_at,pass_hash,salt FROM users WHERE email=? COLLATE NOCASE",
        -1, &st, nullptr);
    bind_text(st, 1, email);
    std::optional<json> out;
    if (sqlite3_step(st) == SQLITE_ROW) {
        auto expect = col_text(st, 5);
        auto salt = col_text(st, 6);
        if (hash_password_(salt, password) == expect) {
            out = json{
                {"id", sqlite3_column_int64(st, 0)},
                {"email", col_text(st, 1)},
                {"name", col_text(st, 2)},
                {"avatar_path", col_text(st, 3)},
                {"created_at", col_text(st, 4)},
            };
        }
    }
    sqlite3_finalize(st);
    return out;
}

std::optional<json> Database::user_by_id(std::int64_t id) {
    // may be called with or without lock — use recursive pattern carefully
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT id,email,name,avatar_path,created_at FROM users WHERE id=?",
        -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, id);
    std::optional<json> out;
    if (sqlite3_step(st) == SQLITE_ROW) out = user_row_(st);
    sqlite3_finalize(st);
    return out;
}

bool Database::update_profile(std::int64_t id, std::string_view name, std::string_view avatar_path) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    sqlite3_stmt* st = nullptr;
    if (!avatar_path.empty()) {
        sqlite3_prepare_v2(db_, "UPDATE users SET name=?, avatar_path=? WHERE id=?", -1, &st, nullptr);
        bind_text(st, 1, name);
        bind_text(st, 2, avatar_path);
        sqlite3_bind_int64(st, 3, id);
    } else {
        sqlite3_prepare_v2(db_, "UPDATE users SET name=? WHERE id=?", -1, &st, nullptr);
        bind_text(st, 1, name);
        sqlite3_bind_int64(st, 2, id);
    }
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE;
}

json Database::create_project(std::int64_t owner_id, std::string_view name,
                              std::string_view description) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "INSERT INTO projects(name,description,owner_id) VALUES(?,?,?)",
        -1, &st, nullptr);
    bind_text(st, 1, name);
    bind_text(st, 2, description);
    sqlite3_bind_int64(st, 3, owner_id);
    sqlite3_step(st);
    sqlite3_finalize(st);
    auto pid = sqlite3_last_insert_rowid(db_);
    sqlite3_prepare_v2(db_,
        "INSERT INTO project_members(project_id,user_id,role) VALUES(?,?, 'owner')",
        -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, pid);
    sqlite3_bind_int64(st, 2, owner_id);
    sqlite3_step(st);
    sqlite3_finalize(st);

    sqlite3_prepare_v2(db_,
        "SELECT id,name,description,owner_id,created_at FROM projects WHERE id=?",
        -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, pid);
    json out;
    if (sqlite3_step(st) == SQLITE_ROW) out = project_row_(st);
    sqlite3_finalize(st);
    return out;
}

json Database::list_projects(std::int64_t user_id) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT p.id,p.name,p.description,p.owner_id,p.created_at "
        "FROM projects p JOIN project_members m ON m.project_id=p.id "
        "WHERE m.user_id=? ORDER BY p.id DESC",
        -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, user_id);
    json arr = json::array();
    while (sqlite3_step(st) == SQLITE_ROW) arr.push_back(project_row_(st));
    sqlite3_finalize(st);
    return arr;
}

bool Database::member_of(std::int64_t project_id, std::int64_t user_id) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT 1 FROM project_members WHERE project_id=? AND user_id=?",
        -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, project_id);
    sqlite3_bind_int64(st, 2, user_id);
    bool ok = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    return ok;
}

std::optional<json> Database::project_by_id(std::int64_t id, std::int64_t user_id) {
    if (!member_of(id, user_id)) return std::nullopt;
    std::lock_guard<std::recursive_mutex> lk(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT id,name,description,owner_id,created_at FROM projects WHERE id=?",
        -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, id);
    std::optional<json> out;
    if (sqlite3_step(st) == SQLITE_ROW) out = project_row_(st);
    sqlite3_finalize(st);
    return out;
}

bool Database::delete_project(std::int64_t id, std::int64_t owner_id) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "DELETE FROM projects WHERE id=? AND owner_id=?", -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, id);
    sqlite3_bind_int64(st, 2, owner_id);
    int rc = sqlite3_step(st);
    int changes = sqlite3_changes(db_);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE && changes > 0;
}

json Database::create_task(std::int64_t project_id, std::int64_t creator_id,
                           std::string_view title, std::string_view body,
                           std::string_view status, int priority,
                           std::optional<std::int64_t> assignee) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "INSERT INTO tasks(project_id,title,body,status,priority,assignee_id,creator_id) "
        "VALUES(?,?,?,?,?,?,?)",
        -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, project_id);
    bind_text(st, 2, title);
    bind_text(st, 3, body);
    bind_text(st, 4, status.empty() ? "todo" : status);
    sqlite3_bind_int(st, 5, priority);
    if (assignee) sqlite3_bind_int64(st, 6, *assignee);
    else sqlite3_bind_null(st, 6);
    sqlite3_bind_int64(st, 7, creator_id);
    sqlite3_step(st);
    sqlite3_finalize(st);
    auto id = sqlite3_last_insert_rowid(db_);
    return *task_by_id(id);
}

json Database::list_tasks(std::int64_t project_id, std::string_view status_filter,
                          std::string_view q) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    std::string sql =
        "SELECT id,project_id,title,body,status,priority,assignee_id,creator_id,created_at,updated_at "
        "FROM tasks WHERE project_id=?";
    if (!status_filter.empty()) sql += " AND status=?";
    if (!q.empty()) sql += " AND (title LIKE ? OR body LIKE ?)";
    sql += " ORDER BY priority DESC, id DESC";

    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &st, nullptr);
    int i = 1;
    sqlite3_bind_int64(st, i++, project_id);
    if (!status_filter.empty()) bind_text(st, i++, status_filter);
    if (!q.empty()) {
        std::string like = "%" + std::string(q) + "%";
        bind_text(st, i++, like);
        bind_text(st, i++, like);
    }
    json arr = json::array();
    while (sqlite3_step(st) == SQLITE_ROW) arr.push_back(task_row_(st));
    sqlite3_finalize(st);
    return arr;
}

std::optional<json> Database::task_by_id(std::int64_t id) {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT id,project_id,title,body,status,priority,assignee_id,creator_id,created_at,updated_at "
        "FROM tasks WHERE id=?",
        -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, id);
    std::optional<json> out;
    if (sqlite3_step(st) == SQLITE_ROW) out = task_row_(st);
    sqlite3_finalize(st);
    return out;
}

std::optional<json> Database::update_task(std::int64_t id, const json& patch) {
    auto cur = task_by_id(id);
    if (!cur) return std::nullopt;
    std::string title = patch.value("title", (*cur)["title"].get<std::string>());
    std::string body = patch.value("body", (*cur)["body"].get<std::string>());
    std::string status = patch.value("status", (*cur)["status"].get<std::string>());
    int priority = patch.value("priority", (*cur)["priority"].get<int>());

    std::lock_guard<std::recursive_mutex> lk(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "UPDATE tasks SET title=?, body=?, status=?, priority=?, "
        "assignee_id=?, updated_at=datetime('now') WHERE id=?",
        -1, &st, nullptr);
    bind_text(st, 1, title);
    bind_text(st, 2, body);
    bind_text(st, 3, status);
    sqlite3_bind_int(st, 4, priority);
    if (patch.contains("assignee_id")) {
        if (patch["assignee_id"].is_null()) sqlite3_bind_null(st, 5);
        else sqlite3_bind_int64(st, 5, patch["assignee_id"].get<std::int64_t>());
    } else if ((*cur)["assignee_id"].is_null()) {
        sqlite3_bind_null(st, 5);
    } else {
        sqlite3_bind_int64(st, 5, (*cur)["assignee_id"].get<std::int64_t>());
    }
    sqlite3_bind_int64(st, 6, id);
    sqlite3_step(st);
    sqlite3_finalize(st);
    return task_by_id(id);
}

bool Database::delete_task(std::int64_t id, std::int64_t /*user_id*/) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "DELETE FROM tasks WHERE id=?", -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, id);
    int rc = sqlite3_step(st);
    int changes = sqlite3_changes(db_);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE && changes > 0;
}

json Database::add_comment(std::int64_t task_id, std::int64_t user_id, std::string_view body) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "INSERT INTO comments(task_id,user_id,body) VALUES(?,?,?)",
        -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, task_id);
    sqlite3_bind_int64(st, 2, user_id);
    bind_text(st, 3, body);
    sqlite3_step(st);
    sqlite3_finalize(st);
    auto cid = sqlite3_last_insert_rowid(db_);
    sqlite3_prepare_v2(db_,
        "SELECT c.id,c.task_id,c.user_id,c.body,c.created_at,u.name "
        "FROM comments c JOIN users u ON u.id=c.user_id WHERE c.id=?",
        -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, cid);
    json out;
    if (sqlite3_step(st) == SQLITE_ROW) {
        out = {
            {"id", sqlite3_column_int64(st, 0)},
            {"task_id", sqlite3_column_int64(st, 1)},
            {"user_id", sqlite3_column_int64(st, 2)},
            {"body", col_text(st, 3)},
            {"created_at", col_text(st, 4)},
            {"user_name", col_text(st, 5)},
        };
    }
    sqlite3_finalize(st);
    return out;
}

json Database::list_comments(std::int64_t task_id) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT c.id,c.task_id,c.user_id,c.body,c.created_at,u.name "
        "FROM comments c JOIN users u ON u.id=c.user_id WHERE c.task_id=? ORDER BY c.id",
        -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, task_id);
    json arr = json::array();
    while (sqlite3_step(st) == SQLITE_ROW) {
        arr.push_back({
            {"id", sqlite3_column_int64(st, 0)},
            {"task_id", sqlite3_column_int64(st, 1)},
            {"user_id", sqlite3_column_int64(st, 2)},
            {"body", col_text(st, 3)},
            {"created_at", col_text(st, 4)},
            {"user_name", col_text(st, 5)},
        });
    }
    sqlite3_finalize(st);
    return arr;
}

json Database::dashboard_stats(std::int64_t user_id) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    json out{{"projects", 0}, {"tasks", 0}, {"todo", 0}, {"doing", 0}, {"done", 0}};
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT COUNT(*) FROM project_members WHERE user_id=?", -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, user_id);
    if (sqlite3_step(st) == SQLITE_ROW) out["projects"] = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);

    sqlite3_prepare_v2(db_,
        "SELECT t.status, COUNT(*) FROM tasks t "
        "JOIN project_members m ON m.project_id=t.project_id "
        "WHERE m.user_id=? GROUP BY t.status",
        -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, user_id);
    std::int64_t total = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        auto status = col_text(st, 0);
        auto n = sqlite3_column_int64(st, 1);
        total += n;
        out[status] = n;
    }
    sqlite3_finalize(st);
    out["tasks"] = total;
    return out;
}

} // namespace nexus
