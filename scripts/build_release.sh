#!/usr/bin/env bash
# Optimized release build (examples included).
#
# Usage: ./scripts/build_release.sh [--install [--prefix=/usr/local]]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${SCRIPT_DIR}/.."
BUILD_DIR="${ROOT}/build-release"

INSTALL="OFF"
PREFIX="/usr/local"

for arg in "$@"; do
  case "$arg" in
    --install)  INSTALL="ON";;
    --prefix=*) PREFIX="${arg#*=}";;
    -h|--help)
      echo "build_release.sh [--install] [--prefix=/path]"
      exit 0;;
  esac
done

cmake -S "${ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DSOCKETIFY_BUILD_EXAMPLES=ON \
    -DCMAKE_INSTALL_PREFIX="${PREFIX}"

cmake --build "${BUILD_DIR}" -j"$(nproc)"

if [ "${INSTALL}" = "ON" ]; then
  cmake --install "${BUILD_DIR}"
fi
