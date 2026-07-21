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
struct TimedUser : Model<TimedUser> {
    static constexpr std::string_view table = "timed";
    static Schema schema() {
        return Schema::create(table)
            .integer("id")
            .primary()
            .autoincrement()
            .text("name")
            .not_null()
            .timestamps();
    }
    static void boot() { validates("name", required()); }
};

struct NullableUser : Model<NullableUser> {
    static constexpr std::string_view table = "nullable_users";
    static Schema schema() {
        return Schema::create(table)
            .integer("id")
            .primary()
            .autoincrement()
            .text("email")
            .not_null()
            .text("nickname");
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

TEST(DbQuery, WhereInAndNull) {
    auto db = Database::open(Sqlite{.path = ":memory:", .pool_size = 1});
    User::migrate_schema(db);
    NullableUser::migrate_schema(db);
    User::create(db, {{"email", "a@example.com"}, {"name", "A"}});
    User::create(db, {{"email", "b@example.com"}, {"name", "B"}});

    auto rows = User::query(db).where_in("email", nlohmann::json::array({"a@example.com"})).get();
    EXPECT_EQ(rows.size(), 1u);

    NullableUser::create(db, {{"email", "null@example.com"}, {"nickname", "nick"}});
    db.exec("UPDATE " + quote_ident(Dialect::Sqlite, "nullable_users") +
            " SET nickname = NULL WHERE email = ?",
            {"null@example.com"});
    auto nulls = Query(&db, "nullable_users").where_null("nickname").get();
    EXPECT_EQ(nulls.size(), 1u);
}

TEST(DbQuery, PaginateAndPluck) {
    auto db = Database::open(Sqlite{.path = ":memory:", .pool_size = 1});
    User::migrate_schema(db);
    for (int i = 0; i < 5; ++i) {
        User::create(db, {{"email", "u" + std::to_string(i) + "@example.com"},
                          {"name", "U" + std::to_string(i)}});
    }
    auto page = User::query(db).order_by("id").paginate(2, 2);
    EXPECT_EQ(page.total, 5);
    EXPECT_EQ(page.page, 2);
    EXPECT_EQ(page.rows.size(), 2u);
    auto emails = User::query(db).pluck("email");
    EXPECT_EQ(emails.size(), 5u);
}

TEST(DbQuery, UpsertAndFirstOrCreate) {
    auto db = Database::open(Sqlite{.path = ":memory:", .pool_size = 1});
    User::migrate_schema(db);
    Query(&db, "users")
        .upsert({{"email", "z@example.com"}, {"name", "Z"}}, {"email"});
    auto row = Query(&db, "users").where_eq("email", "z@example.com").first();
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ((*row)["name"], "Z");

    auto created = Query(&db, "users")
                       .first_or_create({{"email", "new@example.com"}, {"name", "New"}});
    ASSERT_TRUE(created.has_value());
    EXPECT_EQ((*created)["name"], "New");
    auto again = Query(&db, "users")
                     .first_or_create({{"email", "new@example.com"}, {"name", "New"}});
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(User::query(db).count(), 2);
}

TEST(DbOrm, SaveRunsValidatorsAndUpdatedAt) {
    auto db = Database::open(Sqlite{.path = ":memory:", .pool_size = 1});
    TimedUser::migrate_schema(db);
    auto t = TimedUser::create(db, {{"name", "before"}});
    auto before = t->get<std::string>("updated_at");
    t->update({{"name", "after"}});
    auto after = TimedUser::find_or_fail(db, t->id())->get<std::string>("updated_at");
    EXPECT_NE(before, after);
    EXPECT_THROW(t->update({{"name", ""}}), Error);
}

TEST(DbOrm, AffectedRowCounts) {
    auto db = Database::open(Sqlite{.path = ":memory:", .pool_size = 1});
    User::migrate_schema(db);
    auto u = User::create(db, {{"email", "c@example.com"}, {"name", "C"}});
    EXPECT_EQ(User::query(db).where_eq("id", u->id()).update({{"name", "C2"}}), 1);
    EXPECT_EQ(User::query(db).where_eq("id", 99999).destroy(), 0);
}

TEST(DbOrm, MemoryPoolSharesDb) {
    auto db = Database::open(Sqlite{.path = ":memory:", .pool_size = 4});
    User::migrate_schema(db);
    User::create(db, {{"email", "pool@example.com"}, {"name", "Pool"}});
    EXPECT_EQ(User::query(db).count(), 1);
}
