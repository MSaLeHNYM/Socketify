/**
 * @file http_parser_sm.cpp
 * @brief State-machine implementation of the incremental HTTP/1.1 parser.
 */

#include "socketify/detail/http_parser.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace socketify::detail {

// --------- small ASCII helpers ----------
inline char HttpParser::ascii_lower_(char c) noexcept {
  unsigned char uc = static_cast<unsigned char>(c);
  if (uc >= 'A' && uc <= 'Z')
    return static_cast<char>(uc - 'A' + 'a');
  return static_cast<char>(uc);
}

bool HttpParser::iequal_ascii_(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (ascii_lower_(a[i]) != ascii_lower_(b[i]))
      return false;
  return true;
}

// --------- c'tor / reset ----------
HttpParser::HttpParser() = default;

void HttpParser::reset() {
  state_ = ParseState::StartLine;
  line_buf_.clear();
  method_ = Method::UNKNOWN;
  target_.clear();
  path_.clear();
  query_.clear();
  version_.clear();
  headers_.clear();
  chunked_ = false;
  content_length_ = 0;
  body_received_ = 0;
  chunk_remaining_ = 0;
  body_storage_.clear();
  body_ = {};
  err_msg_.clear();
  err_status_ = Status::BadRequest;
  header_bytes_ = 0;
}

// --------- public consume ----------
std::size_t HttpParser::consume(const char *data, std::size_t len) {
  if (!data || len == 0 || state_ == ParseState::Error ||
      state_ == ParseState::Complete)
    return 0;

  std::size_t consumed = 0;
  while (consumed < len && state_ != ParseState::Error &&
         state_ != ParseState::Complete) {
    std::size_t n = 0;
    switch (state_) {
    case ParseState::StartLine:
      n = parse_start_line_(data + consumed, len - consumed);
      break;
    case ParseState::Headers:
      n = parse_headers_(data + consumed, len - consumed);
      break;
    case ParseState::Body:
      n = parse_body_(data + consumed, len - consumed);
      break;
    case ParseState::ChunkSize:
      n = parse_chunk_size_(data + consumed, len - consumed);
      break;
    case ParseState::ChunkData:
      n = parse_chunk_data_(data + consumed, len - consumed);
      break;
    case ParseState::ChunkDataEnd:
      n = parse_chunk_data_end_(data + consumed, len - consumed);
      break;
    case ParseState::ChunkTrailer:
      n = parse_chunk_trailer_(data + consumed, len - consumed);
      break;
    case ParseState::Complete:
    case ParseState::Error:
      break;
    }
    consumed += n;
    if (n == 0)
      break; // need more data
  }
  return consumed;
}

// --------- parse start line: METHOD SP TARGET SP VERSION CRLF ----------
std::size_t HttpParser::parse_start_line_(const char *data, std::size_t len) {
  std::size_t i = 0;
  for (; i < len; ++i) {
    char c = data[i];
    if (!grow_header_bytes_(1))
      return i + 1;
    if (c == '\n') {
      std::string line = std::move(line_buf_);
      line_buf_.clear();
      if (!line.empty() && line.back() == '\r')
        line.pop_back();

      // Tolerate leading empty line(s) between pipelined requests.
      if (line.empty())
        continue;

      size_t p1 = line.find(' ');
      size_t p2 = (p1 == std::string::npos) ? std::string::npos
                                            : line.find(' ', p1 + 1);
      if (p1 == std::string::npos || p2 == std::string::npos) {
        fail_("Malformed start-line", Status::BadRequest);
        return i + 1;
      }

      std::string_view m = std::string_view(line).substr(0, p1);
      std::string_view rt = std::string_view(line).substr(p1 + 1, p2 - (p1 + 1));
      std::string_view ver = std::string_view(line).substr(p2 + 1);

      method_ = method_from_string(m);
      if (method_ == Method::UNKNOWN) {
        fail_("Unknown HTTP method", Status::NotImplemented);
        return i + 1;
      }
      if (rt.empty()) {
        fail_("Empty request-target", Status::BadRequest);
        return i + 1;
      }
      if (!(ver == "HTTP/1.1" || ver == "HTTP/1.0")) {
        fail_("Unsupported HTTP version", static_cast<Status>(505));
        return i + 1;
      }

      version_.assign(ver.begin(), ver.end());
      target_.assign(rt.begin(), rt.end());

      size_t qpos = target_.find('?');
      if (qpos == std::string::npos) {
        path_ = target_;
        query_.clear();
      } else {
        path_.assign(target_.data(), qpos);
        query_.assign(target_.data() + qpos + 1, target_.size() - qpos - 1);
      }

      state_ = ParseState::Headers;
      return i + 1;
    }
    line_buf_.push_back(c);
  }
  return i;
}

// --------- parse headers until blank line ----------
std::size_t HttpParser::parse_headers_(const char *data, std::size_t len) {
  std::size_t i = 0;
  while (i < len) {
    char c = data[i++];
    if (!grow_header_bytes_(1))
      return i;
    if (c != '\n') {
      line_buf_.push_back(c);
      continue;
    }

    std::string line = std::move(line_buf_);
    line_buf_.clear();
    if (!line.empty() && line.back() == '\r')
      line.pop_back();

    if (!line.empty()) {
      size_t colon = line.find(':');
      if (colon == std::string::npos) {
        fail_("Header missing ':'", Status::BadRequest);
        return i;
      }
      size_t key_end = colon;
      while (key_end > 0 && (line[key_end - 1] == ' ' || line[key_end - 1] == '\t'))
        --key_end;
      std::string key = line.substr(0, key_end);

      size_t value_start = colon + 1;
      while (value_start < line.size() &&
             (line[value_start] == ' ' || line[value_start] == '\t'))
        ++value_start;
      size_t value_end = line.size();
      while (value_end > value_start &&
             (line[value_end - 1] == ' ' || line[value_end - 1] == '\t'))
        --value_end;
      std::string value = line.substr(value_start, value_end - value_start);

      if (key.empty()) {
        fail_("Empty header name", Status::BadRequest);
        return i;
      }

      // Repeated headers are joined with ", " (RFC 7230 §3.2.2).
      auto it = headers_.find(key);
      if (it != headers_.end()) {
        it->second.append(", ").append(value);
      } else {
        headers_.emplace(std::move(key), std::move(value));
      }
      continue;
    }

    // ---- End of headers ----
    auto te = headers_.find("Transfer-Encoding");
    if (te != headers_.end()) {
      std::string low = te->second;
      for (auto &ch : low)
        ch = ascii_lower_(ch);
      if (low.find("chunked") != std::string::npos) {
        chunked_ = true;
      } else {
        fail_("Unsupported Transfer-Encoding", Status::NotImplemented);
        return i;
      }
    }

    auto cl = headers_.find("Content-Length");
    if (cl != headers_.end()) {
      if (chunked_) {
        // Reject smuggling-prone combination (RFC 7230 §3.3.3).
        fail_("Both Content-Length and chunked", Status::BadRequest);
        return i;
      }
      const std::string &s = cl->second;
      char *endp = nullptr;
      errno = 0;
      long long v = std::strtoll(s.c_str(), &endp, 10);
      if (endp == s.c_str() || *endp != '\0' || v < 0 || errno == ERANGE) {
        fail_("Invalid Content-Length", Status::BadRequest);
        return i;
      }
      content_length_ = static_cast<std::size_t>(v);
      if (max_body_bytes_ && content_length_ > max_body_bytes_) {
        fail_("Body too large", Status::PayloadTooLarge);
        return i;
      }
    }

    if (chunked_) {
      state_ = ParseState::ChunkSize;
      return i;
    }
    if (content_length_ > 0) {
      body_storage_.reserve(content_length_);
      state_ = ParseState::Body;
      return i;
    }
    body_ = std::string_view(body_storage_.data(), body_storage_.size());
    state_ = ParseState::Complete;
    return i;
  }
  return i;
}

// --------- fixed Content-Length body ----------
std::size_t HttpParser::parse_body_(const char *data, std::size_t len) {
  const std::size_t need = content_length_ - body_received_;
  const std::size_t take = (len < need) ? len : need;

  if (take > 0) {
    body_storage_.append(data, take);
    body_received_ += take;
  }
  if (body_received_ == content_length_) {
    body_ = std::string_view(body_storage_.data(), body_storage_.size());
    state_ = ParseState::Complete;
  }
  return take;
}

// --------- chunked: size line ----------
std::size_t HttpParser::parse_chunk_size_(const char *data, std::size_t len) {
  std::size_t i = 0;
  for (; i < len; ++i) {
    char c = data[i];
    if (c != '\n') {
      if (line_buf_.size() > 128) {
        fail_("Chunk size line too long", Status::BadRequest);
        return i + 1;
      }
      line_buf_.push_back(c);
      continue;
    }
    std::string line = std::move(line_buf_);
    line_buf_.clear();
    if (!line.empty() && line.back() == '\r')
      line.pop_back();

    // Ignore chunk extensions after ';'.
    if (auto sc = line.find(';'); sc != std::string::npos)
      line.resize(sc);

    if (line.empty()) {
      fail_("Missing chunk size", Status::BadRequest);
      return i + 1;
    }
    char *endp = nullptr;
    errno = 0;
    unsigned long long v = std::strtoull(line.c_str(), &endp, 16);
    if (endp == line.c_str() || *endp != '\0' || errno == ERANGE) {
      fail_("Invalid chunk size", Status::BadRequest);
      return i + 1;
    }
    if (!grow_body_bytes_(static_cast<std::size_t>(v)))
      return i + 1;

    chunk_remaining_ = static_cast<std::size_t>(v);
    state_ = (chunk_remaining_ == 0) ? ParseState::ChunkTrailer
                                     : ParseState::ChunkData;
    return i + 1;
  }
  return i;
}

// --------- chunked: payload ----------
std::size_t HttpParser::parse_chunk_data_(const char *data, std::size_t len) {
  const std::size_t take = (len < chunk_remaining_) ? len : chunk_remaining_;
  if (take > 0) {
    body_storage_.append(data, take);
    chunk_remaining_ -= take;
  }
  if (chunk_remaining_ == 0)
    state_ = ParseState::ChunkDataEnd;
  return take;
}

// --------- chunked: CRLF after payload ----------
std::size_t HttpParser::parse_chunk_data_end_(const char *data, std::size_t len) {
  std::size_t i = 0;
  for (; i < len; ++i) {
    char c = data[i];
    if (c == '\r')
      continue;
    if (c == '\n') {
      state_ = ParseState::ChunkSize;
      return i + 1;
    }
    fail_("Malformed chunk terminator", Status::BadRequest);
    return i + 1;
  }
  return i;
}

// --------- chunked: trailers until blank line ----------
std::size_t HttpParser::parse_chunk_trailer_(const char *data, std::size_t len) {
  std::size_t i = 0;
  while (i < len) {
    char c = data[i++];
    if (c != '\n') {
      if (line_buf_.size() > 8 * 1024) {
        fail_("Trailer too long", Status::BadRequest);
        return i;
      }
      line_buf_.push_back(c);
      continue;
    }
    std::string line = std::move(line_buf_);
    line_buf_.clear();
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (line.empty()) {
      // End of trailers -> message complete.
      content_length_ = body_storage_.size();
      body_ = std::string_view(body_storage_.data(), body_storage_.size());
      state_ = ParseState::Complete;
      return i;
    }
    // Trailer fields are accepted and discarded.
  }
  return i;
}

} // namespace socketify::detail
