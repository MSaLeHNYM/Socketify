#include "socketify/detail/http_parser.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
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
bool HttpParser::iequal_ascii_(std::string_view a,
                               std::string_view b) noexcept {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (ascii_lower_(a[i]) != ascii_lower_(b[i]))
      return false;
  return true;
}
inline bool HttpParser::is_token_char_(char c) noexcept {
  // RFC 7230 token (simplified)
  static constexpr const char *tspecials = "()<>@,;:\\\"/[]?={} \t";
  if (c <= 0x1F || c == 0x7F)
    return false;
  for (const char *p = tspecials; *p; ++p)
    if (*p == c)
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
  version_.clear();
  headers_.clear();
  content_length_ = 0;
  body_received_ = 0;
  body_storage_.clear();
  body_ = {};
  err_msg_.clear();
}

// --------- public consume ----------
std::size_t HttpParser::consume(const char *data, std::size_t len) {
  if (!data || len == 0 || state_ == ParseState::Error ||
      state_ == ParseState::Complete)
    return 0;

  std::size_t consumed = 0;
  while (consumed < len && state_ != ParseState::Error &&
         state_ != ParseState::Complete) {
    switch (state_) {
    case ParseState::StartLine: {
      std::size_t n = parse_start_line_(data + consumed, len - consumed);
      consumed += n;
    } break;
    case ParseState::Headers: {
      std::size_t n = parse_headers_(data + consumed, len - consumed);
      consumed += n;
    } break;
    case ParseState::Body: {
      std::size_t n = parse_body_(data + consumed, len - consumed);
      consumed += n;
    } break;
    case ParseState::Complete:
    case ParseState::Error:
      break;
    }
  }
  return consumed;
}

// --------- parse start line: METHOD SP TARGET SP VERSION CRLF ----------
std::size_t HttpParser::parse_start_line_(const char *data, std::size_t len) {
  // Accumulate until we see CRLF
  std::size_t i = 0;
  for (; i < len; ++i) {
    char c = data[i];
    if (c == '\n') {
      // Expect previous char to be '\r' (tolerate LF-only by trimming)
      std::string line = std::move(line_buf_);
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      line_buf_.clear();

      // Parse: METHOD SP TARGET SP VERSION
      // Split by spaces (exactly 2 spaces expected)
      size_t p1 = line.find(' ');
      size_t p2 = (p1 == std::string::npos) ? std::string::npos
                                            : line.find(' ', p1 + 1);
      if (p1 == std::string::npos || p2 == std::string::npos) {
        state_ = ParseState::Error;
        err_msg_ = "Malformed start-line";
        return i + 1;
      }

      std::string_view m = std::string_view(line).substr(0, p1);
      std::string_view rt =
          std::string_view(line).substr(p1 + 1, p2 - (p1 + 1));
      std::string_view ver = std::string_view(line).substr(p2 + 1);

      method_ = method_from_string(m);
      if (method_ == Method::UNKNOWN) {
        state_ = ParseState::Error;
        err_msg_ = "Unknown HTTP method";
        return i + 1;
      }

      // Version must be HTTP/1.1 for now (we accept HTTP/1.0 to be friendly)
      if (!(ver == "HTTP/1.1" || ver == "HTTP/1.0")) {
        state_ = ParseState::Error;
        err_msg_ = "Unsupported HTTP version";
        return i + 1;
      }

      version_.assign(ver.begin(), ver.end());
      target_.assign(rt.begin(), rt.end());

      // path is portion before '?'
      size_t qpos = target_.find('?');
      if (qpos == std::string::npos)
        path_ = target_;
      else
        path_.assign(target_.data(), qpos);

      state_ = ParseState::Headers;
      return i + 1;
    } else {
      line_buf_.push_back(c);
    }
  }
  // Not complete line yet
  return i;
}

// --------- parse headers until blank line ----------
std::size_t HttpParser::parse_headers_(const char *data, std::size_t len) {
  std::size_t i = 0;
  while (i < len) {
    char c = data[i++];
    if (c == '\n') {
      // Process the accumulated line (without the '\n')
      std::string line = std::move(line_buf_);
      line_buf_.clear();

      if (!line.empty() && line.back() == '\r')
        line.pop_back();

      if (line.empty()) {
        // End of headers
        for (const auto &kv : headers_) {
          if (iequal_ascii_(kv.first, "Transfer-Encoding")) {
            std::string low = kv.second;
            for (auto &ch : low)
              ch = ascii_lower_(ch);
            if (low.find("chunked") != std::string::npos) {
              state_ = ParseState::Error;
              err_msg_ = "Chunked transfer-encoding unsupported in v1";
              return i;
            }
          }
        }
        auto it = headers_.find("Content-Length");
        if (it != headers_.end()) {
          const std::string &s = it->second;
          char *endp = nullptr;
          long long v = std::strtoll(s.c_str(), &endp, 10);
          if (endp == s.c_str() || v < 0) {
            state_ = ParseState::Error;
            err_msg_ = "Invalid Content-Length";
            return i;
          }
          content_length_ = static_cast<std::size_t>(v);
          if (content_length_ > 0) {
            body_storage_.reserve(content_length_);
            state_ = ParseState::Body;
            return i;
          }
        }
        // No body
        body_ = std::string_view(body_storage_.data(), body_storage_.size());
        state_ = ParseState::Complete;
        return i;
      } else {
        // "Key: value"
        size_t colon = line.find(':');
        if (colon == std::string::npos) {
          state_ = ParseState::Error;
          err_msg_ = "Header missing ':'";
          return i;
        }
        size_t key_end = colon;
        while (key_end > 0 &&
               (line[key_end - 1] == ' ' || line[key_end - 1] == '\t'))
          --key_end;
        std::string key = line.substr(0, key_end);

        size_t value_start = colon + 1;
        while (value_start < line.size() &&
               (line[value_start] == ' ' || line[value_start] == '\t'))
          ++value_start;
        std::string value = line.substr(value_start);

        headers_[std::move(key)] = std::move(value);
      }
    } else {
      // Accumulate this non-newline character
      line_buf_.push_back(c);
    }
  }
  return i;
}

// --------- parse body for fixed Content-Length ----------
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

} // namespace socketify::detail
