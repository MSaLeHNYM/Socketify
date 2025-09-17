#pragma once
// socketify/detail/http_parser.h — incremental HTTP/1.1 request parser (v1)

#include "socketify/http.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace socketify::detail {

enum class ParseState : std::uint8_t {
  StartLine, // reading: METHOD SP TARGET SP VERSION CRLF
  Headers,   // reading header lines until CRLF CRLF
  Body,      // reading fixed-length body (Content-Length)
  Complete,  // request fully parsed (start-line + headers + body)
  Error      // invalid syntax or unsupported feature
};

class HttpParser {
public:
  HttpParser();

  // Feed bytes from the socket.
  // Returns the number of bytes consumed from [data, data+len).
  // You can call consume() multiple times as data arrives.
  std::size_t consume(const char *data, std::size_t len);

  // --- Status ---
  bool complete() const noexcept { return state_ == ParseState::Complete; }
  bool error() const noexcept { return state_ == ParseState::Error; }
  ParseState state() const noexcept { return state_; }
  std::string_view error_message() const noexcept { return err_msg_; }

  // --- Parsed request line ---
  Method method() const noexcept { return method_; }
  std::string_view target() const noexcept {
    return target_;
  } // raw request-target
  std::string_view path() const noexcept {
    return path_;
  } // target without ?query
  std::string_view version() const noexcept {
    return version_;
  } // e.g. "HTTP/1.1"

  // --- Headers & body ---
  const HeaderMap &headers() const noexcept { return headers_; }
  std::size_t content_length() const noexcept { return content_length_; }
  bool has_body() const noexcept {
    return content_length_ > 0 || !body_.empty();
  }
  std::string_view body_view() const noexcept {
    return body_;
  } // complete only when state==Complete

  // Reset to parse a new request (reuse the object)
  void reset();

private:
  // parsing helpers
  std::size_t parse_start_line_(const char *data, std::size_t len);
  std::size_t parse_headers_(const char *data, std::size_t len);
  std::size_t parse_body_(const char *data, std::size_t len);

  static inline bool is_token_char_(char c) noexcept;
  static inline char ascii_lower_(char c) noexcept;
  static bool iequal_ascii_(std::string_view a, std::string_view b) noexcept;

  // state
  ParseState state_{ParseState::StartLine};
  std::string line_buf_; // accumulates a single line until CRLF
  Method method_{Method::UNKNOWN};
  std::string target_;  // raw request-target
  std::string path_;    // target without query
  std::string version_; // "HTTP/1.1"
  HeaderMap headers_;
  std::size_t content_length_{0};
  std::size_t body_received_{0};
  std::string body_storage_; // owns body
  std::string_view body_;    // view into body_storage_
  std::string err_msg_;      // for diagnostics
};

} // namespace socketify::detail
