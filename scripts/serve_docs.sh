#!/usr/bin/env bash
# Generate Doxygen API docs LOCALLY and serve them on 127.0.0.1 (opens a browser).
#
# Output goes to docs/generated/ which is gitignored — never committed or
# pushed to GitHub. Always regenerate with this script when you need docs.
#
# Usage:
#   ./scripts/serve_docs.sh            # generate + serve on :8765
#   ./scripts/serve_docs.sh 9000       # custom port
#   ./scripts/serve_docs.sh --no-open  # don't open the browser
#   ./scripts/serve_docs.sh --regen-only  # only generate, don't serve
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DOXYFILE="${ROOT}/docs/Doxyfile"
OUT_HTML="${ROOT}/docs/generated/html"
PORT=8765
OPEN_BROWSER=1
REGEN_ONLY=0

for arg in "$@"; do
  case "$arg" in
    --no-open)    OPEN_BROWSER=0 ;;
    --regen-only) REGEN_ONLY=1 ;;
    -h|--help)
      sed -n '2,10p' "$0"
      exit 0
      ;;
    *)
      if [[ "$arg" =~ ^[0-9]+$ ]]; then
        PORT="$arg"
      else
        echo "unknown option: $arg" >&2
        exit 1
      fi
      ;;
  esac
done

# Prefer PATH doxygen, then a local .deps sysroot copy (no sudo installs).
find_doxygen() {
  if command -v doxygen >/dev/null 2>&1; then
    command -v doxygen
    return
  fi
  local local_bin="${ROOT}/.deps/sysroot/usr/bin/doxygen"
  if [[ -x "${local_bin}" ]]; then
    echo "${local_bin}"
    return
  fi
  return 1
}

if ! DOXYGEN="$(find_doxygen)"; then
  echo "error: doxygen not found." >&2
  echo "  install:  sudo apt install doxygen" >&2
  echo "  or place a binary at .deps/sysroot/usr/bin/doxygen" >&2
  exit 1
fi

# Local sysroot builds often need their own shared libs.
if [[ "${DOXYGEN}" == *".deps/sysroot"* ]]; then
  export LD_LIBRARY_PATH="${ROOT}/.deps/sysroot/usr/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}"
fi

if [[ ! -f "${DOXYFILE}" ]]; then
  echo "error: missing ${DOXYFILE}" >&2
  exit 1
fi

echo "==> generating docs with ${DOXYGEN}"
(cd "${ROOT}" && "${DOXYGEN}" "${DOXYFILE}")

# Ensure branding assets land in the HTML tree.
mkdir -p "${OUT_HTML}"
cp -f "${ROOT}/assets/logo.png" "${OUT_HTML}/logo.png"
cp -f "${ROOT}/docs/doxygen/favicon.ico" "${OUT_HTML}/favicon.ico"
cp -f "${ROOT}/docs/doxygen/favicon.png" "${OUT_HTML}/favicon.png"

if [[ ! -f "${OUT_HTML}/index.html" ]]; then
  echo "error: expected ${OUT_HTML}/index.html was not produced" >&2
  exit 1
fi
if [[ ! -f "${OUT_HTML}/logo.png" ]]; then
  echo "error: logo.png missing from HTML output" >&2
  exit 1
fi
echo "==> wrote ${OUT_HTML}/index.html (+ logo + favicon)"

if [[ "${REGEN_ONLY}" -eq 1 ]]; then
  exit 0
fi

URL="http://127.0.0.1:${PORT}/"
echo "==> serving docs at ${URL}  (Ctrl-C to stop)"

open_browser() {
  # Give the server a moment to bind.
  sleep 0.4
  if command -v xdg-open >/dev/null 2>&1; then
    xdg-open "${URL}" >/dev/null 2>&1 || true
  elif command -v open >/dev/null 2>&1; then
    open "${URL}" >/dev/null 2>&1 || true
  elif command -v sensible-browser >/dev/null 2>&1; then
    sensible-browser "${URL}" >/dev/null 2>&1 || true
  else
    echo "    (no browser launcher found — open ${URL} yourself)"
  fi
}

if [[ "${OPEN_BROWSER}" -eq 1 ]]; then
  open_browser &
fi

cd "${OUT_HTML}"
exec python3 -m http.server "${PORT}" --bind 127.0.0.1
