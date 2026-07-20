// 02_rest_api — a CRUD JSON API: route groups, params, body parsing,
// proper status codes.
//
// Try it:
//   curl http://localhost:8080/api/todos
//   curl -X POST http://localhost:8080/api/todos -d '{"title":"write docs"}'
//   curl http://localhost:8080/api/todos/1
//   curl -X PATCH http://localhost:8080/api/todos/1 -d '{"done":true}'
//   curl -X DELETE http://localhost:8080/api/todos/1

#include <socketify/socketify.h>

#include <cstdio>
#include <map>
#include <mutex>

using namespace socketify;
using nlohmann::json;

// A tiny thread-safe in-memory "database".
class TodoDb {
public:
    json list() {
        std::lock_guard<std::mutex> lk(mu_);
        json out = json::array();
        for (auto& [id, todo] : items_) out.push_back(todo);
        return out;
    }
    json create(std::string title) {
        std::lock_guard<std::mutex> lk(mu_);
        long id = next_id_++;
        items_[id] = {{"id", id}, {"title", std::move(title)}, {"done", false}};
        return items_[id];
    }
    std::optional<json> get(long id) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = items_.find(id);
        if (it == items_.end()) return std::nullopt;
        return it->second;
    }
    std::optional<json> update(long id, const json& patch) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = items_.find(id);
        if (it == items_.end()) return std::nullopt;
        if (patch.contains("title")) it->second["title"] = patch["title"];
        if (patch.contains("done")) it->second["done"] = patch["done"];
        return it->second;
    }
    bool remove(long id) {
        std::lock_guard<std::mutex> lk(mu_);
        return items_.erase(id) > 0;
    }

private:
    std::mutex mu_;
    std::map<long, json> items_;
    long next_id_{1};
};

int main() {
    Server server;
    TodoDb db;

    auto& api = server.Group("/api");

    // GET /api/todos — list everything
    api.Get("/todos", [&](Request&, Response& res) {
        res.json(db.list());
    });

    // POST /api/todos — create from a JSON body
    api.Post("/todos", [&](Request& req, Response& res) {
        auto j = body::json(req);
        if (!j || !j->contains("title") || !(*j)["title"].is_string()) {
            res.status(Status::UnprocessableEntity)
               .json({{"error", "body must be JSON with a string 'title'"}});
            return;
        }
        res.status(Status::Created).json(db.create((*j)["title"]));
    });

    // GET /api/todos/:id
    api.Get("/todos/:id", [&](Request& req, Response& res) {
        long id = std::atol(req.params().at("id").c_str());
        if (auto todo = db.get(id)) res.json(*todo);
        else res.status(Status::NotFound).json({{"error", "no such todo"}});
    });

    // PATCH /api/todos/:id — partial update
    api.Patch("/todos/:id", [&](Request& req, Response& res) {
        auto j = body::json(req);
        if (!j) {
            res.status(Status::BadRequest).json({{"error", "invalid JSON"}});
            return;
        }
        long id = std::atol(req.params().at("id").c_str());
        if (auto todo = db.update(id, *j)) res.json(*todo);
        else res.status(Status::NotFound).json({{"error", "no such todo"}});
    });

    // DELETE /api/todos/:id
    api.Delete("/todos/:id", [&](Request& req, Response& res) {
        long id = std::atol(req.params().at("id").c_str());
        if (db.remove(id)) res.status(Status::NoContent).end();
        else res.status(Status::NotFound).json({{"error", "no such todo"}});
    });

    if (!server.Run("0.0.0.0", 8080)) {
        std::fprintf(stderr, "failed to start: %s\n", server.last_error().c_str());
        return 1;
    }
    std::printf("REST API on http://localhost:8080/api/todos\n");
    server.Wait();
    return 0;
}
