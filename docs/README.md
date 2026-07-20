# Socketify documentation

- **[Guide (API.md)](API.md)** — hand-written walkthrough (in the repo).
- **API reference (Doxygen)** — **generate locally**, do not commit the HTML:

  ```bash
  ./scripts/serve_docs.sh          # builds docs/generated/ + serves on 127.0.0.1
  ```

  `docs/generated/` is gitignored and must never be pushed to GitHub.
  Source for the generator: `docs/Doxyfile`, `docs/mainpage.md`, `docs/doxygen/`.

- **[Examples](../examples/)** — graded tour (`../scripts/run_examples.sh 01`–`07`).
- **[Project README](../README.md)** — features, build options, quick start.
