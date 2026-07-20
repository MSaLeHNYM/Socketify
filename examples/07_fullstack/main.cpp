// 07_fullstack — everything combined: static frontend + JSON API +
// sessions + SSE live updates + logging, CORS, rate limiting, compression.
//
// A tiny guestbook: the browser loads the static page, posts entries to
// the API (session tracks your name), and every tab updates live via SSE.
//
// Try it:
//   open http://localhost:8080 in two tabs and post an entry

#include <socketify/socketify.h>

#include <cstdio>
#include <mutex>
#include <vector>

using namespace socketify;
using nlohmann::json;

class Guestbook {
public:
    json add(std::string name, std::string message) {
        std::lock_guard<std::mutex> lk(mu_);
        json entry = {{"id", static_cast<long>(entries_.size()) + 1},
                      {"name", std::move(name)},
                      {"message", std::move(message)}};
        entries_.push_back(entry);
        return entry;
    }
    json list() {
        std::lock_guard<std::mutex> lk(mu_);
        return json(entries_);
    }

private:
    std::mutex mu_;
    std::vector<json> entries_;
};

class Hub {
public:
    void add(sse::Session s) {
        std::lock_guard<std::mutex> lk(mu_);
        sessions_.push_back(std::move(s));
    }
    void broadcast(std::string_view event, const json& j) {
        std::lock_guard<std::mutex> lk(mu_);
        std::string data = j.dump();
        std::erase_if(sessions_, [&](sse::Session& s) { return !s.send_event(event, data); });
    }

private:
    std::mutex mu_;
    std::vector<sse::Session> sessions_;
};

int main() {
    ServerOptions opts;
    opts.compression.min_size = 256;

    Server server(opts);
    Guestbook book;
    Hub hub;

    // ---- Global middleware stack ----
    logging::set_level(logging::Level::Info);
    server.Use(logging::middleware());
    server.Use(middleware::request_id());
    server.Use(cors::middleware());

    ratelimit::Options rl;
    rl.capacity = 30;
    rl.refill_per_second = 5.0;
    server.Use(ratelimit::middleware(rl));

    sessions::Options so;
    so.secret = "fullstack-example-secret-change-me!";
    server.Use(sessions::middleware(so));

    // ---- Static frontend ----
    server.Use(static_files::serve("public"));

    // ---- API ----
    auto& api = server.Group("/api");

    api.Get("/entries", [&](Request&, Response& res) {
        res.json(book.list());
    });

    api.Post("/entries", [&](Request& req, Response& res) {
        auto j = body::json(req);
        if (!j || !j->contains("message")) {
            res.status(Status::UnprocessableEntity).json({{"error", "need a 'message'"}});
            return;
        }
        auto sess = sessions::get(req);
        std::string name = j->value("name", "");
        if (name.empty() && sess->has("name")) name = sess->get("name").get<std::string>();
        if (name.empty()) name = "anonymous";
        sess->set("name", name); // remember for next time

        json entry = book.add(name, (*j)["message"]);
        hub.broadcast("entry", entry);
        res.status(Status::Created).json(entry);
    });

    api.Get("/me", [&](Request& req, Response& res) {
        auto sess = sessions::get(req);
        res.json({{"name", sess->has("name") ? sess->get("name") : json("anonymous")}});
    });

    // ---- Live updates ----
    server.Get("/events", [&](Request& req, Response& res) {
        auto s = sse::upgrade(req, res);
        s.comment("connected");
        hub.add(std::move(s));
    });

    if (!server.Run("0.0.0.0", 8080)) {
        std::fprintf(stderr, "failed to start: %s\n", server.last_error().c_str());
        return 1;
    }
    std::printf("fullstack guestbook on http://localhost:8080\n");
    server.Wait();
    return 0;
}
