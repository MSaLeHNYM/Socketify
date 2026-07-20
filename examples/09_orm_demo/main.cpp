// 09_orm_demo — Socketify multi-backend ORM showcase (SQLite + memory Mongo).
//
// Demonstrates: Schema DSL, Model CRUD, validations, hooks, relations,
// migrations, transactions, and Document API (Mongo memory://).
//
//   ./example_09_orm_demo

#include <socketify/db.h>
#include <socketify/socketify.h>

#include <cstdio>
#include <iostream>

using namespace socketify;
using namespace socketify::db;

struct User : Model<User> {
    static constexpr std::string_view table = "users";
    static Schema schema() {
        return Schema::create(table)
            .integer("id")
            .primary()
            .autoincrement()
            .text("email")
            .unique()
            .not_null()
            .text("name")
            .not_null()
            .timestamps();
    }
    static void boot() {
        validates("email", required(), email());
        validates("name", required(), min_length(1));
        before_save([](Record& r) {
            auto n = r.attrs().value("name", "");
            if (!n.empty() && n.front() >= 'a' && n.front() <= 'z') {
                n.front() = static_cast<char>(n.front() - 'a' + 'A');
                r.set("name", n);
            }
        });
        has_many_on("posts", "user_id", "posts");
    }
};

struct Post : Model<Post> {
    static constexpr std::string_view table = "posts";
    static Schema schema() {
        return Schema::create(table)
            .integer("id")
            .primary()
            .autoincrement()
            .integer("user_id")
            .not_null()
            .text("title")
            .not_null()
            .foreign_key("user_id", "users", "id", true)
            .index("idx_posts_user", {"user_id"});
    }
    static void boot() { belongs_to_on("users", "user_id", "author"); }
};

struct Note : Document<Note> {
    static constexpr std::string_view collection = "notes";
    static void boot() {
        validates("body", required(), min_length(1));
        index(IndexSpec::asc("tag"));
    }
};

int main() {
    // ---- SQL (SQLite) ----
    auto sql = Database::open(Sqlite{.path = "orm_demo.db", .pool_size = 4});
    User::migrate_schema(sql);
    Post::migrate_schema(sql);

    MigrationRegistry::instance().add(Migration{
        "20260720_seed_flag",
        [](Database& db) { db.exec("CREATE TABLE IF NOT EXISTS flags (k TEXT PRIMARY KEY)"); },
        [](Database& db) { db.drop_table("flags"); },
    });
    sql.migrate();

    auto user = User::create(sql, {{"email", "ada@socketify.dev"}, {"name", "ada"}});
    std::cout << "created user id=" << user->id() << " name=" << user->get<std::string>("name")
              << "\n";

    Post::create(sql, {{"user_id", user->id()}, {"title", "Hello ORM"}});
    Post::create(sql, {{"user_id", user->id()}, {"title", "Relations work"}});

    auto posts = user->related("posts");
    std::cout << "user has " << posts.size() << " posts\n";
    for (auto& p : posts) std::cout << "  - " << p["title"] << "\n";

    sql.transaction([&] {
        User::create(sql, {{"email", "temp@socketify.dev"}, {"name", "Temp"}});
        return 0;
    });
    std::cout << "users count=" << User::query(sql).count() << "\n";

    // ---- Document (in-memory Mongo-compatible API) ----
    auto docs = Database::open(Mongo{.uri = "memory://", .db = "demo"});
    Note::ensure_indexes(docs);
    auto note = Note::create(docs, {{"body", "ship it"}, {"tag", "orm"}});
    std::cout << "note id=" << note->attrs().value("id", 0) << "\n";

    // Tiny HTTP surface so the example also exercises the server
    Server server;
    server.Get("/", [&](Request&, Response& res) {
        res.json({{"users", User::query(sql).count()},
                  {"posts", Post::query(sql).count()},
                  {"note", note->attrs()}});
    });

    if (!server.Listen(8090)) {
        std::fprintf(stderr, "listen failed: %s\n", server.last_error().c_str());
        return 1;
    }
    std::printf("orm demo on http://localhost:8090  (also wrote orm_demo.db)\n");
    server.Wait();
    return 0;
}
