// Unit tests for socketify::db ORM (SQLite + memory Mongo).

#include "socketify/db.h"

#include <gtest/gtest.h>

#include <set>

using namespace socketify::db;

namespace {

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
            .foreign_key("user_id", "users", "id", true);
    }
    static void boot() { belongs_to_on("users", "user_id", "user"); }
};

struct DocUser : Document<DocUser> {
    static constexpr std::string_view collection = "users";
    static void boot() {
        validates("email", required(), email());
        index(IndexSpec::asc("email").set_unique(true));
    }
};

struct Hooked : Model<Hooked> {
    static constexpr std::string_view table = "hooked";
    static Schema schema() {
        return Schema::create(table).integer("id").primary().autoincrement().text("name").not_null();
    }
    static int hits;
    static void boot() {
        before_save([](Record& r) {
            ++hits;
            auto n = r.attrs()["name"].get<std::string>();
            r.set("name", n + "!");
        });
    }
};
int Hooked::hits = 0;

} // namespace

TEST(DbSchema, RendersSqliteCreateTable) {
    auto s = Schema::create("t").integer("id").primary().autoincrement().text("name").not_null();
    auto sql = s.to_sql(Dialect::Sqlite);
    EXPECT_NE(sql.find("CREATE TABLE"), std::string::npos);
    EXPECT_NE(sql.find("AUTOINCREMENT"), std::string::npos);
}

TEST(DbOrm, CrudAndQuery) {
    auto db = Database::open(Sqlite{.path = ":memory:", .pool_size = 2});
    User::migrate_schema(db);
    Post::migrate_schema(db);

    auto u = User::create(db, {{"email", "ada@example.com"}, {"name", "Ada"}});
    ASSERT_NE(u, nullptr);
    EXPECT_GT(u->id(), 0);

    auto found = User::find(db, u->id());
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ((*found)->get<std::string>("email"), "ada@example.com");

    auto rows = User::query(db).where_eq("email", "ada@example.com").get();
    ASSERT_EQ(rows.size(), 1u);

    (*found)->update({{"name", "Ada Lovelace"}});
    auto again = User::find_or_fail(db, u->id());
    EXPECT_EQ(again->get<std::string>("name"), "Ada Lovelace");
}

TEST(DbOrm, ValidationRejectsBadEmail) {
    auto db = Database::open(Sqlite{.path = ":memory:", .pool_size = 1});
    User::migrate_schema(db);
    EXPECT_THROW(User::create(db, {{"email", "not-an-email"}, {"name", "X"}}), Error);
}

TEST(DbOrm, RelationsHasManyBelongsTo) {
    auto db = Database::open(Sqlite{.path = ":memory:", .pool_size = 2});
    User::migrate_schema(db);
    Post::migrate_schema(db);

    auto u = User::create(db, {{"email", "b@example.com"}, {"name", "Bob"}});
    auto p1 = Post::create(db, {{"user_id", u->id()}, {"title", "Hello"}});
    auto p2 = Post::create(db, {{"user_id", u->id()}, {"title", "World"}});
    (void)p1;
    (void)p2;

    auto posts = u->related("posts");
    EXPECT_EQ(posts.size(), 2u);

    auto owners = Post::find_or_fail(db, posts[0]["id"].get<std::int64_t>())->related("user");
    ASSERT_EQ(owners.size(), 1u);
    EXPECT_EQ(owners[0]["email"], "b@example.com");
}

TEST(DbOrm, TransactionRollback) {
    auto db = Database::open(Sqlite{.path = ":memory:", .pool_size = 2});
    User::migrate_schema(db);
    try {
        db.transaction([&] {
            User::create(db, {{"email", "t@example.com"}, {"name", "T"}});
            throw Error("boom");
            return 0;
        });
    } catch (const Error&) {
    }
    EXPECT_EQ(User::query(db).count(), 0);
}

TEST(DbOrm, MigrationRegistry) {
    static bool ran = false;
    MigrationRegistry::instance().add(Migration{
        "test_mig_001",
        [](Database& db) {
            db.exec("CREATE TABLE IF NOT EXISTS mig_probe (id INTEGER PRIMARY KEY)");
            ran = true;
        },
        [](Database& db) { db.exec("DROP TABLE IF EXISTS mig_probe"); },
    });
    auto db = Database::open(Sqlite{.path = ":memory:", .pool_size = 1});
    db.migrate();
    EXPECT_TRUE(ran);
    auto rows = db.query("SELECT name FROM sqlite_master WHERE name='mig_probe'");
    EXPECT_FALSE(rows.empty());
}

TEST(DbDocument, MemoryMongoCrud) {
    auto db = Database::open(Mongo{.uri = "memory://", .db = "test"});
    DocUser::ensure_indexes(db);
    auto u = DocUser::create(db, {{"email", "m@example.com"}, {"name", "Mongo"}});
    ASSERT_NE(u, nullptr);
    auto found = DocUser::find_one(db, {{"email", "m@example.com"}});
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ((*found)->get<std::string>("name"), "Mongo");
    (*found)->update({{"name", "Updated"}});
    auto again = DocUser::find_one(db, {{"email", "m@example.com"}});
    EXPECT_EQ((*again)->get<std::string>("name"), "Updated");
    (*again)->destroy();
    EXPECT_FALSE(DocUser::find_one(db, {{"email", "m@example.com"}}).has_value());
}

TEST(DbHooks, BeforeSaveRuns) {
    Hooked::hits = 0;
    auto db = Database::open(Sqlite{.path = ":memory:", .pool_size = 1});
    Hooked::migrate_schema(db);
    auto h = Hooked::create(db, {{"name", "x"}});
    EXPECT_EQ(Hooked::hits, 1);
    EXPECT_EQ(h->get<std::string>("name"), "x!");
}
