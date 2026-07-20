#!/usr/bin/env bash
# Build the examples and run one of them.
#
# Usage: ./scripts/run_examples.sh [01|02|03|04|05|06|07|08]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${SCRIPT_DIR}/.."
BUILD_DIR="${ROOT}/build-release"

declare -A EXAMPLES=(
  [01]=01_hello_world
  [02]=02_rest_api
  [03]=03_middleware
  [04]=04_static_site
  [05]=05_sse_chat
  [06]=06_https
  [07]=07_fullstack
  [08]=08_nexus_board
)

PICK="${1:-01}"
NAME="${EXAMPLES[$PICK]:-}"
if [ -z "${NAME}" ]; then
  echo "unknown example '${PICK}'; choose one of: ${!EXAMPLES[*]}"
  exit 1
fi

cmake -S "${ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DSOCKETIFY_BUILD_EXAMPLES=ON >/dev/null
cmake --build "${BUILD_DIR}" -j"$(nproc)" --target "example_${NAME}"

BIN_DIR="${BUILD_DIR}/examples/${NAME}"
echo "running example_${NAME} (Ctrl-C to stop)"
cd "${BIN_DIR}" && "./example_${NAME}"
