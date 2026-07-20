#!/usr/bin/env bash
# Build the examples and run one of them.
#
# Usage: ./scripts/run_examples.sh [01|02|03|04|05|06|07|08|09|10|ripple]
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
  [09]=09_orm_demo
  [10]=10_pulse_chat
  [ripple]=ripple
  [11]=ripple
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
# Ripple (and similar) may rename the binary via OUTPUT_NAME.
BIN="${BIN_DIR}/example_${NAME}"
if [ ! -x "${BIN}" ] && [ -x "${BIN_DIR}/${NAME}" ]; then
  BIN="${BIN_DIR}/${NAME}"
elif [ ! -x "${BIN}" ] && [ -x "${BIN_DIR}/ripple" ]; then
  BIN="${BIN_DIR}/ripple"
fi
echo "running ${BIN} (Ctrl-C to stop)"
cd "${BIN_DIR}" && "${BIN}"
