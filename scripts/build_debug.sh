#!/bin/bash
set -e

# Usage:
#   ./scripts/build_debug.sh [--examples=ON|OFF] [--sudo=ON|OFF] [--prefix=/custom/prefix]
# Defaults: examples=OFF, sudo=ON, prefix=/usr/local

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="${SCRIPT_DIR}/.."
BUILD_DIR="${PROJECT_ROOT}/build-Debug"

EXAMPLES="OFF"
USE_SUDO="ON"
PREFIX="/usr/local"

for arg in "$@"; do
  case "$arg" in
    --examples=ON|--examples=OFF) EXAMPLES="${arg#*=}";;
    --sudo=ON|--sudo=OFF)         USE_SUDO="${arg#*=}";;
    --prefix=*)                    PREFIX="${arg#*=}";;
    -h|--help)
      echo "build_debug.sh [--examples=ON|OFF] [--sudo=ON|OFF] [--prefix=/path]"
      exit 0;;
  esac
done

echo "==> Removing old build directory: ${BUILD_DIR}"
rm -rf "${BUILD_DIR}"

echo "==> Creating build directory"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

echo "==> Configuring (Debug)  examples=${EXAMPLES}  prefix=${PREFIX}"
cmake -DCMAKE_BUILD_TYPE=Debug \
      -DSOCKETIFY_BUILD_EXAMPLES=${EXAMPLES} \
      -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
      ..

echo "==> Building with $(nproc) cores"
cmake --build . -j"$(nproc)"

echo "==> Installing to ${PREFIX}"
if [ "${USE_SUDO}" = "ON" ] && [ "${PREFIX}" = "/usr/local" ]; then
  sudo cmake --install .
else
  cmake --install .
fi

echo "==> Done."

#sudo bash scripts/build_debug.sh --examples=ON
