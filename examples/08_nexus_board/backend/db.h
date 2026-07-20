#pragma once
// Thin SQLite helper for the Nexus Board example.

#include <nlohmann/json.hpp>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace nexus {

class Database {
public:
    explicit Database(std::string path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    void migrate();

    // ---- auth / users ----
    std::optional<nlohmann::json> create_user(std::string_view email, std::string_view name,
                                              std::string_view password);
    std::optional<nlohmann::json> authenticate(std::string_view email, std::string_view password);
    std::optional<nlohmann::json> user_by_id(std::int64_t id);
    bool update_profile(std::int64_t id, std::string_view name, std::string_view avatar_path);

    // ---- projects ----
    nlohmann::json create_project(std::int64_t owner_id, std::string_view name,
                                  std::string_view description);
    nlohmann::json list_projects(std::int64_t user_id);
    std::optional<nlohmann::json> project_by_id(std::int64_t id, std::int64_t user_id);
    bool delete_project(std::int64_t id, std::int64_t owner_id);

    // ---- tasks ----
    nlohmann::json create_task(std::int64_t project_id, std::int64_t creator_id,
                               std::string_view title, std::string_view body,
                               std::string_view status, int priority,
                               std::optional<std::int64_t> assignee);
    nlohmann::json list_tasks(std::int64_t project_id, std::string_view status_filter,
                              std::string_view q);
    std::optional<nlohmann::json> task_by_id(std::int64_t id);
    std::optional<nlohmann::json> update_task(std::int64_t id, const nlohmann::json& patch);
    bool delete_task(std::int64_t id, std::int64_t user_id);

    // ---- comments ----
    nlohmann::json add_comment(std::int64_t task_id, std::int64_t user_id, std::string_view body);
    nlohmann::json list_comments(std::int64_t task_id);

    // ---- stats ----
    nlohmann::json dashboard_stats(std::int64_t user_id);

    bool member_of(std::int64_t project_id, std::int64_t user_id);

private:
    sqlite3* db_{nullptr};
    std::recursive_mutex mu_;

    void exec_(std::string_view sql);
    nlohmann::json user_row_(sqlite3_stmt* st);
    nlohmann::json project_row_(sqlite3_stmt* st);
    nlohmann::json task_row_(sqlite3_stmt* st);

    static std::string hash_password_(std::string_view salt, std::string_view password);
    static std::string make_salt_();
};

} // namespace nexus
