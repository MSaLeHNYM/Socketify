# Socketify documentation

- **[Guide (API.md)](API.md)** — hand-written walkthrough: getting started,
  routing, middleware, request/response, body parsing, sessions, static
  files, SSE, TLS and deployment tips.
- **API reference (Doxygen)** — generated from the comments in
  `include/socketify/`:

  ```bash
  doxygen docs/Doxyfile
  # open docs/generated/html/index.html
  ```

  or through CMake:

  ```bash
  cmake -B build -DSOCKETIFY_BUILD_DOCS=ON
  cmake --build build --target docs
  ```

- **[Examples](../examples/)** — seven runnable programs, from hello world
  to a fullstack guestbook (`../scripts/run_examples.sh 01`–`07`).
- **[Project README](../README.md)** — features, build options, quick start.
