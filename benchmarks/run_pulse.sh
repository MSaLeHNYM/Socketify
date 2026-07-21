#!/usr/bin/env bash
# Pulse vs Node ws vs Python websockets — same-machine echo benchmark.
# Writes benchmarks/pulse_results.{csv,json} and assets/benchmark_pulse_*.svg
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BENCH="${ROOT}/benchmarks"
BUILD="${ROOT}/build-bench"
DURATION="${DURATION:-6}"
CLIENTS="${CLIENTS:-64}"
export PATH="${HOME}/.local/bin:${PATH}"

if [[ -d "${ROOT}/.deps/sysroot/usr" ]]; then
  export CMAKE_PREFIX_PATH="${ROOT}/.deps/sysroot/usr${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
  export OPENSSL_ROOT_DIR="${ROOT}/.deps/sysroot/usr"
  export LD_LIBRARY_PATH="${ROOT}/.deps/sysroot/usr/lib/x86_64-linux-gnu:${ROOT}/.deps/sysroot/usr/lib:${LD_LIBRARY_PATH:-}"
fi

mkdir -p "${BENCH}/servers" "${ROOT}/assets"

echo "==> python deps (websockets)"
if [[ ! -d "${BENCH}/.venv" ]]; then
  python3 -m venv "${BENCH}/.venv"
fi
# shellcheck disable=SC1091
source "${BENCH}/.venv/bin/activate"
pip -q install --upgrade pip
pip -q install 'websockets>=12'

echo "==> npm ws"
if [[ ! -d "${BENCH}/node_modules/ws" ]]; then
  (cd "${BENCH}" && npm init -y >/dev/null 2>&1; npm install --silent ws@8)
fi

echo "==> build Socketify (Release)"
cmake -S "${ROOT}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release \
    -DSOCKETIFY_BUILD_EXAMPLES=OFF -DSOCKETIFY_BUILD_TESTS=OFF
cmake --build "${BUILD}" -j"$(nproc)" --target socketify

echo "==> compile socketify_pulse"
LIB="${BUILD}/libsocketify.a"
[[ -f "${LIB}" ]] || LIB="${BUILD}/libsocketify.so"

SYSROOT_INC=()
SYSROOT_LIB=()
if [[ -d "${ROOT}/.deps/sysroot/usr/include" ]]; then
  SYSROOT_INC+=(-I"${ROOT}/.deps/sysroot/usr/include")
  SYSROOT_LIB+=(-L"${ROOT}/.deps/sysroot/usr/lib" -L"${ROOT}/.deps/sysroot/usr/lib/x86_64-linux-gnu")
fi

g++ -std=c++20 -O3 -DNDEBUG \
    -I"${ROOT}/include" "${SYSROOT_INC[@]}" \
    "${BENCH}/servers/socketify_pulse.cpp" \
    "${LIB}" \
    "${SYSROOT_LIB[@]}" \
    -lssl -lcrypto -lz -pthread \
    -o "${BENCH}/servers/socketify_pulse"

echo "==> run Pulse WebSocket load (duration=${DURATION}s clients=${CLIENTS})"
python3 "${BENCH}/run_pulse_load.py" \
    --skip-build \
    --duration "${DURATION}" \
    --clients "${CLIENTS}"

echo "==> Hub fan-out microbench"
g++ -std=c++20 -O3 -DNDEBUG \
    -I"${ROOT}/include" "${SYSROOT_INC[@]}" \
    "${BENCH}/servers/pulse_hub_fanout.cpp" \
    "${LIB}" \
    "${SYSROOT_LIB[@]}" \
    -lssl -lcrypto -lz -pthread \
    -o "${BENCH}/servers/pulse_hub_fanout"
"${BENCH}/servers/pulse_hub_fanout" 1000 2000 | tee "${BENCH}/pulse_hub_fanout.txt"

# Refresh hub CSV + SVG from last known good numbers if microbench printed CSV line
python3 - <<'PY'
from pathlib import Path
import re, csv, sys
sys.path.insert(0, "benchmarks")
from render_charts import write_bar_chart
root = Path(".")
text = (root / "benchmarks/pulse_hub_fanout.txt").read_text()
# peers=1000 ... per_peer / hub lines
m_once = re.search(r"hub_broadcast_frame:\s+([0-9.]+)", text)
m_peer = re.search(r"per_peer_send_text:\s+([0-9.]+)", text)
m_peers = re.search(r"peers=(\d+)", text)
if m_once and m_peer:
    once, peer = float(m_once.group(1)), float(m_peer.group(1))
    peers = int(m_peers.group(1)) if m_peers else 1000
    path = root / "benchmarks/pulse_hub_results.csv"
    with path.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["name","delivered_msg_per_sec","peers","rounds","payload_bytes","notes"])
        w.writeheader()
        w.writerow({"name":"Pulse Hub broadcast_frame (encode-once)","delivered_msg_per_sec":f"{once:.0f}","peers":peers,"rounds":2000,"payload_bytes":256,"notes":"fan-out one frame to N peers"})
        w.writerow({"name":"Naive per-peer send_text (re-encode)","delivered_msg_per_sec":f"{peer:.0f}","peers":peers,"rounds":2000,"payload_bytes":256,"notes":"encode WebSocket frame once per peer"})
    write_bar_chart(
        path=root/"assets/benchmark_pulse_hub.svg",
        title="Pulse Hub fan-out (delivered msg/s)",
        subtitle=f"{peers:,} peers · 256 B · encode-once broadcast_frame vs N× send_text",
        rows=[("Hub encode-once", once, "#0D9488"), ("Per-peer re-encode", peer, "#F59E0B")],
        higher_better=True,
    )
    print(f"updated {path}")
PY
