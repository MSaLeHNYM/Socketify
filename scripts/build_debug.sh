#!/usr/bin/env bash
# Debug build with sanitizers, tests and examples. CI-friendly.
#
# Usage: ./scripts/build_debug.sh [--no-san] [--no-tests] [--examples]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${SCRIPT_DIR}/.."
BUILD_DIR="${ROOT}/build-debug"

SANITIZE="address,undefined"
TESTS="ON"
EXAMPLES="OFF"

for arg in "$@"; do
  case "$arg" in
    --no-san)   SANITIZE="";;
    --no-tests) TESTS="OFF";;
    --examples) EXAMPLES="ON";;
    -h|--help)
      echo "build_debug.sh [--no-san] [--no-tests] [--examples]"
      exit 0;;
  esac
done

cmake -S "${ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSOCKETIFY_BUILD_TESTS="${TESTS}" \
    -DSOCKETIFY_BUILD_EXAMPLES="${EXAMPLES}" \
    -DSOCKETIFY_SANITIZE="${SANITIZE}"

cmake --build "${BUILD_DIR}" -j"$(nproc)"

if [ "${TESTS}" = "ON" ]; then
  ctest --test-dir "${BUILD_DIR}" --output-on-failure
fi
