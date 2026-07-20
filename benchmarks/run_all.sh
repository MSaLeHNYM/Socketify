#!/usr/bin/env bash
# Same-machine JSON /ping benchmark: Socketify vs Flask / Express / Django.
# Writes benchmarks/results.json and assets/benchmark_{rps,latency}.svg
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BENCH="${ROOT}/benchmarks"
BUILD="${ROOT}/build-bench"
DURATION="${DURATION:-8}"
CONCURRENCY="${CONCURRENCY:-100}"
export PATH="${HOME}/.local/bin:${PATH}"
# Local OpenSSL sysroot (no sudo) if present
if [[ -d "${ROOT}/.deps/sysroot/usr" ]]; then
  export CMAKE_PREFIX_PATH="${ROOT}/.deps/sysroot/usr${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
  export OPENSSL_ROOT_DIR="${ROOT}/.deps/sysroot/usr"
  export LD_LIBRARY_PATH="${ROOT}/.deps/sysroot/usr/lib/x86_64-linux-gnu:${ROOT}/.deps/sysroot/usr/lib:${LD_LIBRARY_PATH:-}"
fi

mkdir -p "${BENCH}/servers" "${ROOT}/assets"

echo "==> python venv + Flask/Django/Waitress"
if [[ ! -d "${BENCH}/.venv" ]]; then
  python3 -m venv "${BENCH}/.venv"
fi
# shellcheck disable=SC1091
source "${BENCH}/.venv/bin/activate"
pip -q install --upgrade pip
pip -q install 'flask>=3' 'waitress>=3' 'django>=4.2'

echo "==> npm express"
if [[ ! -d "${BENCH}/node_modules/express" ]]; then
  (cd "${BENCH}" && npm init -y >/dev/null 2>&1 && npm install --silent express@4)
fi

echo "==> build Socketify (Release)"
cmake -S "${ROOT}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release \
    -DSOCKETIFY_BUILD_EXAMPLES=OFF -DSOCKETIFY_BUILD_TESTS=OFF
cmake --build "${BUILD}" -j"$(nproc)" --target socketify

echo "==> compile socketify_ping"
SYSROOT_INC=()
SYSROOT_LIB=()
if [[ -d "${ROOT}/.deps/sysroot/usr/include" ]]; then
  SYSROOT_INC+=(-I"${ROOT}/.deps/sysroot/usr/include")
  SYSROOT_LIB+=(-L"${ROOT}/.deps/sysroot/usr/lib" -L"${ROOT}/.deps/sysroot/usr/lib/x86_64-linux-gnu")
fi

LIB="${BUILD}/libsocketify.a"
if [[ ! -f "${LIB}" ]]; then
  LIB="${BUILD}/libsocketify.so"
fi

g++ -std=c++20 -O3 -DNDEBUG \
    -I"${ROOT}/include" "${SYSROOT_INC[@]}" \
    "${BENCH}/servers/socketify_ping.cpp" \
    "${LIB}" \
    "${SYSROOT_LIB[@]}" \
    -lssl -lcrypto -lz -pthread \
    -o "${BENCH}/servers/socketify_ping"

echo "==> ensure wrk load generator"
if [[ ! -x "${BENCH}/wrk" ]]; then
  if [[ -x /tmp/wrk-4.2.0/wrk ]]; then
    cp /tmp/wrk-4.2.0/wrk "${BENCH}/wrk"
  else
    echo "building wrk…"
    TMP="$(mktemp -d)"
    curl -fsSL -o "${TMP}/wrk.tgz" https://github.com/wg/wrk/archive/refs/tags/4.2.0.tar.gz
    tar -xzf "${TMP}/wrk.tgz" -C "${TMP}"
    make -C "${TMP}/wrk-4.2.0" -j"$(nproc)"
    cp "${TMP}/wrk-4.2.0/wrk" "${BENCH}/wrk"
    rm -rf "${TMP}"
  fi
  chmod +x "${BENCH}/wrk"
fi

echo "==> run load tests (duration=${DURATION}s concurrency=${CONCURRENCY})"
python3 "${BENCH}/run_load.py" \
    --root "${ROOT}" \
    --duration "${DURATION}" \
    --concurrency "${CONCURRENCY}"
