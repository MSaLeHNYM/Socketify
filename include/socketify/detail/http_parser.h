#pragma once
/**
 * @file http_parser.h
 * @brief Incremental HTTP/1.1 request parser (start-line, headers,
 *        Content-Length and chunked bodies) with configurable limits.
 *
 * Feed bytes with consume() as they arrive from the socket; the parser
 * consumes only what belongs to the current message, so pipelined requests
 * are handled correctly by the caller (leftover bytes stay in its buffer).
 */

#include "socketify/http.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace socketify::detail {

/** @brief Parser progress states. */
enum class ParseState : std::uint8_t {
  StartLine,    ///< Reading: METHOD SP TARGET SP VERSION CRLF
  Headers,      ///< Reading header lines until CRLF CRLF
  Body,         ///< Reading fixed-length body (Content-Length)
  ChunkSize,    ///< Reading a chunk-size line (chunked encoding)
  ChunkData,    ///< Reading chunk payload
  ChunkDataEnd, ///< Expecting CRLF after chunk payload
  ChunkTrailer, ///< Reading trailer lines after the last chunk
  Complete,     ///< Request fully parsed
  Error         ///< Invalid syntax, unsupported feature, or limit exceeded
};

/**
 * @brief Incremental HTTP/1.1 request parser.
 *
 * @code
 * detail::HttpParser p;
 * p.set_limits(16 * 1024, 8 * 1024 * 1024);
 * std::size_t used = p.consume(buf.data(), buf.size());
 * buf.consume(used);
 * if (p.complete()) { ... p.method(), p.path(), p.headers(), p.body_view() ... }
 * else if (p.error()) { ... respond with p.error_status() ... }
 * @endcode
 */
class HttpParser {
public:
  HttpParser();

  /** @brief Cap header bytes and body bytes; exceeded limits set Error. */
  void set_limits(std::size_t max_header_bytes, std::size_t max_body_bytes) {
    max_header_bytes_ = max_header_bytes;
    max_body_bytes_ = max_body_bytes;
  }

  /**
   * @brief Feed bytes from the socket.
   * @return Number of bytes consumed from [data, data+len). Bytes past the
   *         end of the current message are never consumed.
   */
  std::size_t consume(const char *data, std::size_t len);

  // --- Status ---
  /** @brief True when a full request was parsed. */
  bool complete() const noexcept { return state_ == ParseState::Complete; }
  /** @brief True when the input was invalid or a limit was exceeded. */
  bool error() const noexcept { return state_ == ParseState::Error; }
  /** @brief Current state. */
  ParseState state() const noexcept { return state_; }
  /** @brief Human-readable description of the error. */
  std::string_view error_message() const noexcept { return err_msg_; }
  /** @brief HTTP status suited to the failure (400/413/431/501/505). */
  Status error_status() const noexcept { return err_status_; }

  // --- Parsed request line ---
  /** @brief Parsed method. */
  Method method() const noexcept { return method_; }
  /** @brief Raw request-target as received. */
  std::string_view target() const noexcept { return target_; }
  /** @brief Target without the query string (still percent-encoded). */
  std::string_view path() const noexcept { return path_; }
  /** @brief Query-string portion after '?' ("" when absent). */
  std::string_view query_string() const noexcept { return query_; }
  /** @brief Version string, e.g. "HTTP/1.1". */
  std::string_view version() const noexcept { return version_; }

  // --- Headers & body ---
  /** @brief Parsed headers (case-insensitive keys). */
  const HeaderMap &headers() const noexcept { return headers_; }
  /** @brief Declared Content-Length (0 for chunked until complete). */
  std::size_t content_length() const noexcept { return content_length_; }
  /** @brief True when the request carries a body. */
  bool has_body() const noexcept { return content_length_ > 0 || !body_storage_.empty(); }
  /** @brief Body contents; complete only when state()==Complete. */
  std::string_view body_view() const noexcept { return body_; }
  /** @brief Take ownership of the body (parser keeps an empty body). */
  std::string take_body() {
    std::string b = std::move(body_storage_);
    body_storage_.clear();
    body_ = {};
    return b;
  }

  /** @brief Reset to parse the next request on the same connection. */
  void reset();

private:
  std::size_t parse_start_line_(const char *data, std::size_t len);
  std::size_t parse_headers_(const char *data, std::size_t len);
  std::size_t parse_body_(const char *data, std::size_t len);
  std::size_t parse_chunk_size_(const char *data, std::size_t len);
  std::size_t parse_chunk_data_(const char *data, std::size_t len);
  std::size_t parse_chunk_data_end_(const char *data, std::size_t len);
  std::size_t parse_chunk_trailer_(const char *data, std::size_t len);

  void fail_(std::string msg, Status st) {
    state_ = ParseState::Error;
    err_msg_ = std::move(msg);
    err_status_ = st;
  }
  bool grow_header_bytes_(std::size_t n) {
    header_bytes_ += n;
    if (max_header_bytes_ && header_bytes_ > max_header_bytes_) {
      fail_("Header section too large", static_cast<Status>(431));
      return false;
    }
    return true;
  }
  bool grow_body_bytes_(std::size_t n) {
    if (max_body_bytes_ && body_storage_.size() + n > max_body_bytes_) {
      fail_("Body too large", Status::PayloadTooLarge);
      return false;
    }
    return true;
  }

  static inline char ascii_lower_(char c) noexcept;
  static bool iequal_ascii_(std::string_view a, std::string_view b) noexcept;

  // state
  ParseState state_{ParseState::StartLine};
  std::string line_buf_;
  Method method_{Method::UNKNOWN};
  std::string target_;
  std::string path_;
  std::string query_;
  std::string version_;
  HeaderMap headers_;
  bool chunked_{false};
  std::size_t content_length_{0};
  std::size_t body_received_{0};
  std::size_t chunk_remaining_{0};
  std::string body_storage_;
  std::string_view body_;
  std::string err_msg_;
  Status err_status_{Status::BadRequest};

  std::size_t header_bytes_{0};
  std::size_t max_header_bytes_{16 * 1024};
  std::size_t max_body_bytes_{16 * 1024 * 1024};
};

} // namespace socketify::detail
