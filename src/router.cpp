#include "socketify/router.h"

#include <algorithm>
#include <cassert>

namespace socketify {

// ---------- Route ----------
Route::Route(Method m, std::string pattern, Handler h)
    : method_(m), pattern_(std::move(pattern)), handler_(std::move(h)) {}

// ---------- Router helpers ----------
static inline char ascii_lower(char c) {
  unsigned char uc = static_cast<unsigned char>(c);
  if (uc >= 'A' && uc <= 'Z')
    return static_cast<char>(uc - 'A' + 'a');
  return static_cast<char>(uc);
}
static inline bool iequal_ascii(std::string_view a, std::string_view b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (ascii_lower(a[i]) != ascii_lower(b[i]))
      return false;
  }
  return true;
}

std::vector<Route::Seg> Router::compile_pattern_(std::string_view pattern) {
  std::vector<Route::Seg> segs;
  if (pattern.empty()) {
    // Treat empty as "/"
    segs.push_back({Route::Seg::Static, ""});
    return segs;
  }

  // Normalize: ensure leading slash is not required for splitting behavior
  auto parts = split_path_(pattern);
  segs.reserve(parts.size());

  for (auto part : parts) {
    if (part.empty())
      continue;
    if (part.front() == ':') {
      segs.push_back({Route::Seg::Param, std::string(part.substr(1))});
    } else if (part.front() == '*') {
      // Wildcard must be the last segment; capture the rest
      segs.push_back({Route::Seg::Wildcard, std::string(part.substr(1))});
      // ignore any following parts; wildcard eats the rest
      break;
    } else {
      segs.push_back({Route::Seg::Static, std::string(part)});
    }
  }
  return segs;
}

std::vector<std::string_view> Router::split_path_(std::string_view s) {
  std::vector<std::string_view> out;
  size_t start = 0;
  while (start < s.size()) {
    size_t pos = s.find('/', start);
    if (pos == std::string_view::npos) {
      // push the tail only if non-empty
      if (start < s.size())
        out.emplace_back(s.substr(start));
      break;
    } else {
      if (pos > start)
        out.emplace_back(s.substr(start, pos - start));
      start = pos + 1;
    }
  }
  // Special case: if the entire string was "/" (or multiple slashes), out stays
  // empty.
  return out;
}

bool Router::starts_with_(std::string_view s, std::string_view pfx) {
  return s.size() >= pfx.size() &&
         std::equal(pfx.begin(), pfx.end(), s.begin());
}

// Match `path` against compiled `segs`. If matched, fill req.params().
bool Router::match_and_bind_(std::string_view path,
                             const std::vector<Route::Seg> &segs,
                             Request &req) {
  // Root pattern (e.g., "/") compiles to empty segments → match only "/"
  if (segs.empty()) {
    return path == "/" || path.empty();
  }

  auto parts = split_path_(path);
  auto &params = const_cast<ParamMap &>(req.params());
  params.clear();

  size_t i = 0, j = 0;
  while (i < parts.size() && j < segs.size()) {
    const auto &seg = segs[j];
    switch (seg.kind) {
    case Route::Seg::Static:
      if (!iequal_ascii(parts[i], seg.text))
        return false;
      ++i;
      ++j;
      break;
    case Route::Seg::Param:
      params[seg.text] = std::string(parts[i]);
      ++i;
      ++j;
      break;
    case Route::Seg::Wildcard: {
      // capture the rest joined by '/'
      std::string rest;
      for (; i < parts.size(); ++i) {
        if (!rest.empty())
          rest.push_back('/');
        rest.append(parts[i].data(), parts[i].size());
      }
      params[seg.text] = std::move(rest);
      ++j; // wildcard is last anyway
      i = parts.size();
      break;
    }
    }
  }

  // If pattern consumed all segments (wildcard can end early), it's a match
  if (j < segs.size()) {
    // remaining pattern segments must be exactly one trailing wildcard with
    // empty capture
    if (!(segs[j].kind == Route::Seg::Wildcard && j + 1 == segs.size())) {
      return false;
    }
    // wildcard with no remainder is fine; capture empty
    params[segs[j].text] = "";
    ++j;
  }
  return i == parts.size() && j == segs.size();
}

// ---------- Router public API ----------
Route &Router::AddRoute(Method m, std::string_view pattern, Handler h) {
  routes_.emplace_back(m, std::string(pattern), std::move(h));
  routes_.back().segs_ = compile_pattern_(routes_.back().pattern_);
  return routes_.back();
}

bool Router::dispatch(Request &req, Response &res) const {
  // Find the first route that matches method+path
  const Method method = req.method();
  const std::string_view path = req.path();

  const Route *matched = nullptr;
  for (const auto &r : routes_) {
    if (r.method() != Method::ANY && r.method() != method)
      continue;
    // match path to pattern
    // We need a temporary Request to avoid mutating params unless it matches
    Request temp = req;
    if (match_and_bind_(path, r.segs_, temp)) {
      // apply captured params to real request
      const_cast<ParamMap &>(req.params()) =
          const_cast<ParamMap &>(temp.params());
      matched = &r;
      break;
    }
  }

  if (!matched) {
    // No route matched
    return false;
  }

  // Build the middleware chain: global -> group -> route -> handler
  std::vector<Middleware> chain;
  chain.reserve(global_mw_.size() + matched->middlewares().size() + 1);
  for (const auto &mw : global_mw_)
    chain.push_back(mw);

  // group middlewares (any group whose prefix matches the route pattern)
  for (const auto &g : groups_) {
    // A simple check: if the route pattern starts with group prefix, apply its
    // middleware.
    if (starts_with_(matched->pattern(), g.prefix())) {
      for (const auto &mw : g.middlewares())
        chain.push_back(mw);
    }
  }

  for (const auto &mw : matched->middlewares())
    chain.push_back(mw);

  // finally, the route handler wrapped as the last "middleware"
  chain.push_back([matched](Request &rq, Response &rs, Next) {
    matched->handler()(rq, rs);
  });

  // Execute chain
  size_t idx = 0;
  std::function<void()> step;
  step = [&]() {
    if (idx >= chain.size() || res.ended())
      return;
    auto &mw = chain[idx++];
    mw(req, res, step);
  };
  step();

  return true;
}

} // namespace socketify
