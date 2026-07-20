// 08_nexus_board — Socketify backend + React frontend showcase.
//
// Demonstrates: routing/groups, sessions auth, SQLite, CORS, rate limit,
// logging, request IDs, JSON/form/multipart bodies, cookies, compression,
// static files (React build), SSE live updates, send_file downloads.
//
// Run (from the binary directory after building the React app):
//   ./example_08_nexus_board
//   open http://localhost:8080

#include "db.h"

#include <socketify/socketify.h>
#include <socketify/detail/utils.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

namespace fs = std::filesystem;
using namespace socketify;
using nlohmann::json;

namespace {

class Hub {
public:
    void add(sse::Session s) {
        std::lock_guard<std::mutex> lk(mu_);
        sessions_.push_back(std::move(s));
    }
    void broadcast(std::string_view event, const json& j) {
        std::lock_guard<std::mutex> lk(mu_);
        std::string data = j.dump();
        std::erase_if(sessions_, [&](sse::Session& s) {
            return !s.send_event(event, data);
        });
    }

private:
    std::mutex mu_;
    std::vector<sse::Session> sessions_;
};

std::optional<std::int64_t> uid(Request& req) {
    auto sess = sessions::get(req);
    if (!sess || !sess->has("user_id")) return std::nullopt;
    return sess->get("user_id").get<std::int64_t>();
}

Middleware require_auth() {
    return [](Request& req, Response& res, Next next) {
        if (!uid(req)) {
            res.status(Status::Unauthorized).json(json{{"error", "login required"}});
            return;
        }
        next();
    };
}

std::string find_web_root() {
    const char* env = std::getenv("NEXUS_WEB_ROOT");
    if (env && *env) return env;
    for (auto cand : {"web", "frontend/dist", "../frontend/dist", "../../frontend/dist"}) {
        if (fs::exists(fs::path(cand) / "index.html")) return cand;
    }
    return "web";
}

} // namespace

int main() {
    fs::create_directories("data");
    fs::create_directories("uploads");

    nexus::Database db("data/nexus.db");
    db.migrate();
    Hub hub;

    ServerOptions opts;
    opts.compression.min_size = 512;
    opts.max_body_size = 8 * 1024 * 1024; // avatars

    Server server(opts);

    logging::set_level(logging::Level::Info);
    server.Use(logging::middleware());
    server.Use(middleware::request_id());
    server.Use(middleware::body_limit(8 * 1024 * 1024));

    cors::CorsOptions cors_opts;
    cors_opts.reflect_origin = true;
    cors_opts.allow_credentials = true;
    cors_opts.allow_headers = "Content-Type, X-Requested-With";
    server.Use(cors::middleware(cors_opts));

    ratelimit::Options rl;
    rl.capacity = 120;
    rl.refill_per_second = 40.0;
    server.Use(ratelimit::middleware(rl));

    sessions::Options so;
    so.secret = "nexus-board-demo-secret-change-in-production!!";
    so.cookie_name = "nexus_sid";
    so.ttl = std::chrono::hours(24 * 14);
    so.cookie_http_only = true;
    so.same_site = SameSite::Lax;
    so.cookie_path = "/";
    server.Use(sessions::middleware(so));

    // Stricter limiter for auth endpoints
    ratelimit::Options auth_rl;
    auth_rl.capacity = 10;
    auth_rl.refill_per_second = 0.2;
    auth_rl.message = "too many auth attempts — slow down";
    auto auth_limit = ratelimit::middleware(auth_rl);

    // ---------- public ----------
    server.Get("/api/health", [](Request&, Response& res) {
        res.json(json{{"ok", true}, {"service", "nexus-board"}, {"version", "0.2.0"}});
    });

    auto& auth = server.Group("/api/auth");
    auth.Post("/register", [&](Request& req, Response& res) {
        auto j = body::json(req);
        if (!j || !j->contains("email") || !j->contains("password") || !j->contains("name")) {
            res.status(Status::UnprocessableEntity)
                .json(json{{"error", "need email, name, password"}});
            return;
        }
        auto user = db.create_user((*j)["email"].get<std::string>(), (*j)["name"].get<std::string>(), (*j)["password"].get<std::string>());
        if (!user) {
            res.status(Status::Conflict).json(json{{"error", "email already registered"}});
            return;
        }
        auto sess = sessions::get(req);
        sess->set("user_id", (*user)["id"]);
        res.status(Status::Created).json(json{{"user", *user}});
    }).Use(auth_limit);

    auth.Post("/login", [&](Request& req, Response& res) {
        auto j = body::json(req);
        if (!j || !j->contains("email") || !j->contains("password")) {
            res.status(Status::UnprocessableEntity).json(json{{"error", "need email + password"}});
            return;
        }
        auto user = db.authenticate((*j)["email"].get<std::string>(), (*j)["password"].get<std::string>());
        if (!user) {
            res.status(Status::Unauthorized).json(json{{"error", "invalid credentials"}});
            return;
        }
        auto sess = sessions::get(req);
        sess->set("user_id", (*user)["id"]);
        res.set_cookie(Cookie("nexus_theme", "dark").path("/").max_age(60 * 60 * 24 * 365));
        res.json(json{{"user", *user}});
    }).Use(auth_limit);

    auth.Post("/logout", [&](Request& req, Response& res) {
        if (auto sess = sessions::get(req)) sess->destroy();
        res.clear_cookie("nexus_sid");
        res.json(json{{"ok", true}});
    });

    auth.Get("/me", [&](Request& req, Response& res) {
        auto id = uid(req);
        if (!id) {
            res.status(Status::Unauthorized).json(json{{"user", nullptr}});
            return;
        }
        auto user = db.user_by_id(*id);
        res.json(json{{"user", user ? *user : nullptr}});
    });

    // ---------- protected API (auth applied per-route; Group("/api") MW
    // would also wrap /api/health and /api/auth/*) ----------
    auto& api = server.Group("/api");
    auto guard = require_auth();

    api.Get("/stats", [&](Request& req, Response& res) {
        res.json(db.dashboard_stats(*uid(req)));
    }).Use(guard);

    api.Patch("/profile", [&](Request& req, Response& res) {
        auto j = body::json(req);
        if (!j || !j->contains("name")) {
            res.status(Status::UnprocessableEntity).json(json{{"error", "need name"}});
            return;
        }
        db.update_profile(*uid(req), (*j)["name"].get<std::string>(), "");
        res.json(json{{"user", *db.user_by_id(*uid(req))}});
    }).Use(guard);

    api.Post("/profile/avatar", [&](Request& req, Response& res) {
        auto mp = body::multipart(req);
        if (!mp || mp->files.empty()) {
            res.status(Status::UnprocessableEntity).json(json{{"error", "upload a file field named avatar"}});
            return;
        }
        const auto& f = mp->files.front();
        auto id = *uid(req);
        std::string ext = ".bin";
        if (f.filename.find('.') != std::string::npos)
            ext = f.filename.substr(f.filename.rfind('.'));
        std::string rel = "uploads/avatar_" + std::to_string(id) + ext;
        {
            std::ofstream out(rel, std::ios::binary);
            out.write(f.data.data(), static_cast<std::streamsize>(f.data.size()));
        }
        db.update_profile(id, db.user_by_id(id)->at("name").get<std::string>(), "/" + rel);
        hub.broadcast("profile", json{{"user_id", id}, {"avatar_path", "/" + rel}});
        res.json(json{{"user", *db.user_by_id(id)}});
    }).Use(guard);

    api.Get("/projects", [&](Request& req, Response& res) {
        res.json(json{{"projects", db.list_projects(*uid(req))}});
    }).Use(guard);

    api.Post("/projects", [&](Request& req, Response& res) {
        auto j = body::json(req);
        if (!j || !j->contains("name")) {
            res.status(Status::UnprocessableEntity).json(json{{"error", "need name"}});
            return;
        }
        auto p = db.create_project(*uid(req), (*j)["name"].get<std::string>(), j->value("description", ""));
        hub.broadcast("project", json{{"action", "created"}, {"project", p}});
        res.status(Status::Created).json(json{{"project", p}});
    }).Use(guard);

    api.Get("/projects/:id", [&](Request& req, Response& res) {
        auto id = std::stoll(std::string(req.params().at("id")));
        auto p = db.project_by_id(id, *uid(req));
        if (!p) {
            res.status(Status::NotFound).json(json{{"error", "not found"}});
            return;
        }
        res.json(json{{"project", *p}});
    }).Use(guard);

    api.Delete("/projects/:id", [&](Request& req, Response& res) {
        auto id = std::stoll(std::string(req.params().at("id")));
        if (!db.delete_project(id, *uid(req))) {
            res.status(Status::Forbidden).json(json{{"error", "only the owner can delete"}});
            return;
        }
        hub.broadcast("project", json{{"action", "deleted"}, {"id", id}});
        res.send_status(Status::NoContent);
    }).Use(guard);

    api.Get("/projects/:id/tasks", [&](Request& req, Response& res) {
        auto pid = std::stoll(std::string(req.params().at("id")));
        if (!db.member_of(pid, *uid(req))) {
            res.status(Status::Forbidden).json(json{{"error", "forbidden"}});
            return;
        }
        auto status = std::string(req.query_value("status"));
        auto q = std::string(req.query_value("q"));
        res.json(json{{"tasks", db.list_tasks(pid, status, q)}});
    }).Use(guard);

    api.Post("/projects/:id/tasks", [&](Request& req, Response& res) {
        auto pid = std::stoll(std::string(req.params().at("id")));
        if (!db.member_of(pid, *uid(req))) {
            res.status(Status::Forbidden).json(json{{"error", "forbidden"}});
            return;
        }
        auto j = body::json(req);
        if (!j || !j->contains("title")) {
            res.status(Status::UnprocessableEntity).json(json{{"error", "need title"}});
            return;
        }
        std::optional<std::int64_t> assignee;
        if (j->contains("assignee_id") && !(*j)["assignee_id"].is_null())
            assignee = (*j)["assignee_id"].get<std::int64_t>();
        auto task = db.create_task(pid, *uid(req), (*j)["title"].get<std::string>(), j->value("body", ""),
                                   j->value("status", "todo"), j->value("priority", 1), assignee);
        hub.broadcast("task", json{{"action", "created"}, {"task", task}});
        res.status(Status::Created).json(json{{"task", task}});
    }).Use(guard);

    api.Patch("/tasks/:id", [&](Request& req, Response& res) {
        auto id = std::stoll(std::string(req.params().at("id")));
        auto existing = db.task_by_id(id);
        if (!existing || !db.member_of((*existing)["project_id"], *uid(req))) {
            res.status(Status::NotFound).json(json{{"error", "not found"}});
            return;
        }
        auto j = body::json(req);
        if (!j) {
            res.status(Status::UnprocessableEntity).json(json{{"error", "JSON body required"}});
            return;
        }
        auto updated = db.update_task(id, *j);
        hub.broadcast("task", json{{"action", "updated"}, {"task", *updated}});
        res.json(json{{"task", *updated}});
    }).Use(guard);

    api.Delete("/tasks/:id", [&](Request& req, Response& res) {
        auto id = std::stoll(std::string(req.params().at("id")));
        auto existing = db.task_by_id(id);
        if (!existing || !db.member_of((*existing)["project_id"], *uid(req))) {
            res.status(Status::NotFound).json(json{{"error", "not found"}});
            return;
        }
        db.delete_task(id, *uid(req));
        hub.broadcast("task", json{{"action", "deleted"}, {"id", id},
                               {"project_id", (*existing)["project_id"]}});
        res.send_status(Status::NoContent);
    }).Use(guard);

    api.Get("/tasks/:id/comments", [&](Request& req, Response& res) {
        auto id = std::stoll(std::string(req.params().at("id")));
        auto existing = db.task_by_id(id);
        if (!existing || !db.member_of((*existing)["project_id"], *uid(req))) {
            res.status(Status::NotFound).json(json{{"error", "not found"}});
            return;
        }
        res.json(json{{"comments", db.list_comments(id)}});
    }).Use(guard);

    api.Post("/tasks/:id/comments", [&](Request& req, Response& res) {
        auto id = std::stoll(std::string(req.params().at("id")));
        auto existing = db.task_by_id(id);
        if (!existing || !db.member_of((*existing)["project_id"], *uid(req))) {
            res.status(Status::NotFound).json(json{{"error", "not found"}});
            return;
        }
        auto j = body::json(req);
        if (!j || !j->contains("body")) {
            res.status(Status::UnprocessableEntity).json(json{{"error", "need body"}});
            return;
        }
        auto c = db.add_comment(id, *uid(req), (*j)["body"].get<std::string>());
        hub.broadcast("comment", json{{"action", "created"}, {"comment", c}});
        res.status(Status::Created).json(json{{"comment", c}});
    }).Use(guard);

    api.Get("/export/projects.json", [&](Request& req, Response& res) {
        auto projects = db.list_projects(*uid(req));
        json dump = json::array();
        for (auto& p : projects) {
            auto pid = p["id"].get<std::int64_t>();
            dump.push_back({
                {"project", p},
                {"tasks", db.list_tasks(pid, "", "")},
            });
        }
        std::string path = "data/export_" + std::to_string(*uid(req)) + ".json";
        {
            std::ofstream out(path);
            out << dump.dump(2);
        }
        res.send_file(path, /*download=*/true, "nexus-export.json");
    }).Use(guard);

    // Live board feed
    server.Get("/api/events", [&](Request& req, Response& res) {
        if (!uid(req)) {
            res.status(Status::Unauthorized).json(json{{"error", "login required"}});
            return;
        }
        auto s = sse::upgrade(req, res);
        s.comment("nexus connected");
        s.send_event("hello", json({{"user_id", *uid(req)}}).dump());
        hub.add(std::move(s));
    });

    // Uploaded avatars
    server.Use(static_files::serve("uploads", {.mount = "/uploads", .cache_max_age = 3600}));

    // React production build
    std::string web = find_web_root();
    server.Use(static_files::serve(web, {.mount = "/", .fallthrough = true, .cache_max_age = 60}));

    // SPA fallback (non-API paths → index.html)
    server.Get("/*path", [web](Request& req, Response& res) {
        auto path = std::string(req.path());
        if (path.rfind("/api", 0) == 0) {
            res.status(Status::NotFound).json(json{{"error", "not found"}});
            return;
        }
        if (!res.send_file(web + "/index.html")) {
            res.status(Status::NotFound)
                .html("<h1>Frontend not built</h1><p>Run: <code>cd frontend && npm i && npm run build</code></p>");
        }
    });

    const uint16_t port = 8080;
    if (!server.Listen(port)) {
        std::fprintf(stderr, "failed to start: %s\n", server.last_error().c_str());
        return 1;
    }
    logging::info("Nexus Board on http://localhost:{}  (web root: {})", port, web);
    server.Wait();
    return 0;
}
