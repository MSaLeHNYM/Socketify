// Unit tests for the static files middleware: traversal protection,
// conditional requests, ranges and file responses.

#include "socketify/static_files.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

using namespace socketify;
namespace fs = std::filesystem;

class StaticFilesTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / ("socketify_test_" + std::to_string(::getpid()));
        fs::create_directories(root_ / "sub");
        write_file(root_ / "index.html", "<html>home</html>");
        write_file(root_ / "data.txt", "0123456789");
        write_file(root_ / "sub" / "nested.txt", "nested");
        write_file(root_ / ".secret", "hidden");
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }

    static void write_file(const fs::path& p, std::string_view content) {
        std::ofstream f(p, std::ios::binary);
        f.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    Request make_req(Method m, std::string path) {
        Request r;
        r.set_method(m);
        r.set_path(std::move(path));
        return r;
    }

    fs::path root_;
};

TEST_F(StaticFilesTest, ServesFileAsFileResponse) {
    auto mw = static_files::serve(root_.string());
    auto req = make_req(Method::GET, "/data.txt");
    Response res;
    mw(req, res, [] { FAIL() << "next() should not be called"; });

    EXPECT_TRUE(res.ended());
    EXPECT_EQ(res.kind(), Response::Kind::File);
    EXPECT_EQ(res.file_length(), 10u);
    EXPECT_EQ(res.headers().find("Content-Type")->second, "text/plain; charset=utf-8");
    EXPECT_FALSE(res.headers().find("ETag")->second.empty());
}

TEST_F(StaticFilesTest, ServesIndexForDirectory) {
    auto mw = static_files::serve(root_.string());
    auto req = make_req(Method::GET, "/");
    Response res;
    mw(req, res, [] {});
    EXPECT_TRUE(res.ended());
    EXPECT_EQ(res.kind(), Response::Kind::File);
    EXPECT_EQ(res.headers().find("Content-Type")->second, "text/html; charset=utf-8");
}

TEST_F(StaticFilesTest, BlocksPathTraversal) {
    auto mw = static_files::serve(root_.string());
    bool fell_through = false;
    auto req = make_req(Method::GET, "/../etc/passwd");
    Response res;
    mw(req, res, [&] { fell_through = true; });
    EXPECT_TRUE(fell_through);
    EXPECT_FALSE(res.ended());
}

TEST_F(StaticFilesTest, BlocksDotfilesByDefault) {
    auto mw = static_files::serve(root_.string());
    bool fell_through = false;
    auto req = make_req(Method::GET, "/.secret");
    Response res;
    mw(req, res, [&] { fell_through = true; });
    EXPECT_TRUE(fell_through);
}

TEST_F(StaticFilesTest, MissingFileFallsThrough) {
    auto mw = static_files::serve(root_.string());
    bool fell_through = false;
    auto req = make_req(Method::GET, "/nope.txt");
    Response res;
    mw(req, res, [&] { fell_through = true; });
    EXPECT_TRUE(fell_through);
}

TEST_F(StaticFilesTest, MissingFile404WhenNoFallthrough) {
    static_files::Options o;
    o.root = root_.string();
    o.fallthrough = false;
    auto mw = static_files::serve(o);
    auto req = make_req(Method::GET, "/nope.txt");
    Response res;
    mw(req, res, [] { FAIL(); });
    EXPECT_EQ(res.status_code(), 404);
}

TEST_F(StaticFilesTest, MountPrefix) {
    static_files::Options o;
    o.root = root_.string();
    o.mount = "/assets";
    auto mw = static_files::serve(o);

    auto req = make_req(Method::GET, "/assets/data.txt");
    Response res;
    mw(req, res, [] {});
    EXPECT_TRUE(res.ended());
    EXPECT_EQ(res.kind(), Response::Kind::File);

    // Outside the mount: pass through.
    bool fell_through = false;
    auto req2 = make_req(Method::GET, "/data.txt");
    Response res2;
    mw(req2, res2, [&] { fell_through = true; });
    EXPECT_TRUE(fell_through);
}

TEST_F(StaticFilesTest, ETagConditionalReturns304) {
    auto mw = static_files::serve(root_.string());

    auto req1 = make_req(Method::GET, "/data.txt");
    Response res1;
    mw(req1, res1, [] {});
    std::string etag = res1.headers().find("ETag")->second;
    ASSERT_FALSE(etag.empty());

    auto req2 = make_req(Method::GET, "/data.txt");
    req2.mutable_headers()["If-None-Match"] = etag;
    Response res2;
    mw(req2, res2, [] {});
    EXPECT_EQ(res2.status_code(), 304);
}

TEST_F(StaticFilesTest, RangeRequest) {
    auto mw = static_files::serve(root_.string());
    auto req = make_req(Method::GET, "/data.txt");
    req.mutable_headers()["Range"] = "bytes=2-5";
    Response res;
    mw(req, res, [] {});
    EXPECT_EQ(res.status_code(), 206);
    EXPECT_EQ(res.headers().find("Content-Range")->second, "bytes 2-5/10");
    EXPECT_EQ(res.file_offset(), 2u);
    EXPECT_EQ(res.file_length(), 4u);
}

TEST_F(StaticFilesTest, SuffixRange) {
    auto mw = static_files::serve(root_.string());
    auto req = make_req(Method::GET, "/data.txt");
    req.mutable_headers()["Range"] = "bytes=-3";
    Response res;
    mw(req, res, [] {});
    EXPECT_EQ(res.status_code(), 206);
    EXPECT_EQ(res.file_offset(), 7u);
    EXPECT_EQ(res.file_length(), 3u);
}

TEST_F(StaticFilesTest, InvalidRangeGets416) {
    auto mw = static_files::serve(root_.string());
    auto req = make_req(Method::GET, "/data.txt");
    req.mutable_headers()["Range"] = "bytes=50-60";
    Response res;
    mw(req, res, [] {});
    EXPECT_EQ(res.status_code(), 416);
    EXPECT_EQ(res.headers().find("Content-Range")->second, "bytes */10");
}

TEST_F(StaticFilesTest, PostFallsThrough) {
    auto mw = static_files::serve(root_.string());
    bool fell_through = false;
    auto req = make_req(Method::POST, "/data.txt");
    Response res;
    mw(req, res, [&] { fell_through = true; });
    EXPECT_TRUE(fell_through);
}

TEST_F(StaticFilesTest, NestedDirectories) {
    auto mw = static_files::serve(root_.string());
    auto req = make_req(Method::GET, "/sub/nested.txt");
    Response res;
    mw(req, res, [] {});
    EXPECT_TRUE(res.ended());
    EXPECT_EQ(res.file_length(), 6u);
}
