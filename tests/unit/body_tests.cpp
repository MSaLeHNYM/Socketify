// Unit tests for body parsers: JSON, urlencoded forms and multipart.

#include "socketify/body.h"

#include <gtest/gtest.h>

using namespace socketify;

namespace {

Request req_with(std::string content_type, std::string data) {
    Request r;
    if (!content_type.empty()) {
        r.mutable_headers()["Content-Type"] = std::move(content_type);
    }
    r.set_body_storage(std::move(data));
    return r;
}

} // namespace

TEST(BodyJson, ParsesValidJson) {
    auto req = req_with("application/json", R"({"name":"x","n":42})");
    auto j = body::json(req);
    ASSERT_TRUE(j.has_value());
    EXPECT_EQ((*j)["name"], "x");
    EXPECT_EQ((*j)["n"], 42);
}

TEST(BodyJson, RejectsInvalidJson) {
    auto req = req_with("application/json", "{nope");
    EXPECT_FALSE(body::json(req).has_value());
}

TEST(BodyJson, RejectsEmptyBody) {
    auto req = req_with("application/json", "");
    EXPECT_FALSE(body::json(req).has_value());
}

TEST(BodyForm, ParsesUrlencoded) {
    auto req = req_with("application/x-www-form-urlencoded", "name=John+Doe&age=30&x=%2F");
    auto f = body::form(req);
    EXPECT_EQ(f.at("name"), "John Doe");
    EXPECT_EQ(f.at("age"), "30");
    EXPECT_EQ(f.at("x"), "/");
}

TEST(BodyDetect, ContentTypePredicates) {
    EXPECT_TRUE(body::is_json(req_with("application/json; charset=utf-8", "")));
    EXPECT_TRUE(body::is_form(req_with("application/x-www-form-urlencoded", "")));
    EXPECT_TRUE(body::is_multipart(req_with("multipart/form-data; boundary=x", "")));
    EXPECT_FALSE(body::is_json(req_with("text/plain", "")));
}

TEST(BodyMultipart, ParsesFieldsAndFiles) {
    std::string b = "----boundary42";
    std::string payload =
        "--" + b + "\r\n"
        "Content-Disposition: form-data; name=\"title\"\r\n"
        "\r\n"
        "My Upload\r\n"
        "--" + b + "\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"a.txt\"\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "file contents here\r\n"
        "--" + b + "--\r\n";

    auto req = req_with("multipart/form-data; boundary=" + b, payload);
    auto mp = body::multipart(req);
    ASSERT_TRUE(mp.has_value());
    EXPECT_EQ(mp->fields.at("title"), "My Upload");
    ASSERT_EQ(mp->files.size(), 1u);
    EXPECT_EQ(mp->files[0].name, "file");
    EXPECT_EQ(mp->files[0].filename, "a.txt");
    EXPECT_EQ(mp->files[0].content_type, "text/plain");
    EXPECT_EQ(mp->files[0].data, "file contents here");
}

TEST(BodyMultipart, QuotedBoundary) {
    std::string payload =
        "--qb\r\n"
        "Content-Disposition: form-data; name=\"k\"\r\n"
        "\r\n"
        "v\r\n"
        "--qb--\r\n";
    auto req = req_with("multipart/form-data; boundary=\"qb\"", payload);
    auto mp = body::multipart(req);
    ASSERT_TRUE(mp.has_value());
    EXPECT_EQ(mp->fields.at("k"), "v");
}

TEST(BodyMultipart, BinaryDataWithCrlf) {
    std::string data = "line1\r\nline2\r\n\r\nbinary\x01\x02";
    std::string payload =
        "--b\r\n"
        "Content-Disposition: form-data; name=\"f\"; filename=\"bin\"\r\n"
        "\r\n" +
        data + "\r\n"
        "--b--\r\n";
    auto req = req_with("multipart/form-data; boundary=b", payload);
    auto mp = body::multipart(req);
    ASSERT_TRUE(mp.has_value());
    ASSERT_EQ(mp->files.size(), 1u);
    EXPECT_EQ(mp->files[0].data, data);
}

TEST(BodyMultipart, RejectsMissingBoundary) {
    auto req = req_with("multipart/form-data", "--x\r\n");
    EXPECT_FALSE(body::multipart(req).has_value());
}

TEST(BodyMultipart, RejectsNonMultipart) {
    auto req = req_with("application/json", "{}");
    EXPECT_FALSE(body::multipart(req).has_value());
}

TEST(BodyMultipart, RejectsTruncatedPayload) {
    std::string payload =
        "--b\r\n"
        "Content-Disposition: form-data; name=\"k\"\r\n"
        "\r\n"
        "value with no terminator";
    auto req = req_with("multipart/form-data; boundary=b", payload);
    EXPECT_FALSE(body::multipart(req).has_value());
}
