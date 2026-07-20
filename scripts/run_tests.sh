#!/usr/bin/env bash
# Configure (if needed), build and run the full test suite.
#
# Usage: ./scripts/run_tests.sh [ctest-args...]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${SCRIPT_DIR}/.."
BUILD_DIR="${ROOT}/build-debug"

if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
  cmake -S "${ROOT}" -B "${BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE=Debug \
      -DSOCKETIFY_BUILD_TESTS=ON
fi

cmake --build "${BUILD_DIR}" -j"$(nproc)"
ctest --test-dir "${BUILD_DIR}" --output-on-failure "$@"
