#!/usr/bin/env python3
"""WebSocket echo load test: Pulse vs Node `ws` vs Python `websockets`.

Assumes binaries/packages already prepared by benchmarks/run_pulse.sh
(or pass without --skip-build to compile Pulse server).

Writes:
  benchmarks/pulse_results.csv
  benchmarks/pulse_results.json
  assets/benchmark_pulse_msgs.svg
  assets/benchmark_pulse_latency.svg
"""
from __future__ import annotations

import argparse
import asyncio
import csv
import json
import os
import signal
import statistics
import subprocess
import sys
import time
from pathlib import Path

import websockets
from websockets.exceptions import ConnectionClosed

ROOT = Path(__file__).resolve().parents[1]
BENCH = ROOT / "benchmarks"
ASSETS = ROOT / "assets"


async def wait_ws(url: str, timeout: float = 20.0) -> None:
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        try:
            async with websockets.connect(url, open_timeout=1, ping_interval=None) as ws:
                await ws.send("ping")
                await asyncio.wait_for(ws.recv(), timeout=1.0)
                return
        except Exception as e:  # noqa: BLE001
            last = e
            await asyncio.sleep(0.05)
    raise RuntimeError(f"ws not ready at {url}: {last}")


async def client_worker(
    url: str,
    payload: str,
    stop_at: float,
    latencies: list[float],
    counters: list[int],
    idx: int,
) -> None:
    ok = 0
    try:
        async with websockets.connect(
            url, open_timeout=5, ping_interval=None, max_size=2**20
        ) as ws:
            while time.time() < stop_at:
                t0 = time.perf_counter()
                await ws.send(payload)
                await ws.recv()
                latencies.append((time.perf_counter() - t0) * 1000.0)
                ok += 1
    except ConnectionClosed:
        pass
    except Exception:
        pass
    counters[idx] = ok


async def run_load(url: str, clients: int, duration: float, payload: str) -> dict:
    latencies: list[float] = []
    counters = [0] * clients
    await wait_ws(url)
    stop_at = time.time() + duration
    t0 = time.perf_counter()
    await asyncio.gather(
        *[
            client_worker(url, payload, stop_at, latencies, counters, i)
            for i in range(clients)
        ]
    )
    wall = time.perf_counter() - t0
    total = sum(counters)
    msgs = total / wall if wall > 0 else 0.0
    latencies.sort()

    def pct(p: float) -> float:
        if not latencies:
            return 0.0
        i = min(len(latencies) - 1, max(0, int(round((p / 100.0) * (len(latencies) - 1)))))
        return latencies[i]

    return {
        "msgs_per_sec": round(msgs, 2),
        "messages_ok": total,
        "clients": clients,
        "duration_s": duration,
        "latency_avg_ms": round(statistics.fmean(latencies), 4) if latencies else 0.0,
        "latency_p50_ms": round(pct(50), 4),
        "latency_p99_ms": round(pct(99), 4),
        "wall_s": round(wall, 3),
    }


def start_proc(cmd: list[str], env: dict | None = None) -> subprocess.Popen:
    return subprocess.Popen(
        cmd,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        env=env,
        preexec_fn=os.setsid,
    )


def stop_proc(p: subprocess.Popen) -> None:
    if p.poll() is not None:
        return
    try:
        os.killpg(p.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        p.wait(timeout=3)
    except subprocess.TimeoutExpired:
        os.killpg(p.pid, signal.SIGKILL)


def write_csv(path: Path, rows: list[dict]) -> None:
    fields = [
        "name",
        "msgs_per_sec",
        "messages_ok",
        "clients",
        "duration_s",
        "latency_avg_ms",
        "latency_p50_ms",
        "latency_p99_ms",
    ]
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for r in rows:
            w.writerow({k: r.get(k, "") for k in fields})


def render_charts(results: list[dict]) -> None:
    sys.path.insert(0, str(BENCH))
    from render_charts import write_bar_chart

    colors = {
        "Pulse (Socketify)": "#0D9488",
        "ws (Node.js)": "#16A34A",
        "websockets (Python)": "#2563EB",
    }
    short = {
        "Pulse (Socketify)": "Pulse",
        "ws (Node.js)": "Node ws",
        "websockets (Python)": "Python WS",
    }
    msg_rows = [
        (short[r["name"]], float(r["msgs_per_sec"]), colors[r["name"]])
        for r in sorted(results, key=lambda x: x["msgs_per_sec"], reverse=True)
    ]
    lat_rows = [
        (short[r["name"]], float(r["latency_p99_ms"]), colors[r["name"]])
        for r in sorted(results, key=lambda x: x["latency_p99_ms"])
    ]
    write_bar_chart(
        path=ASSETS / "benchmark_pulse_msgs.svg",
        title="WebSocket echo throughput (msg/s)",
        subtitle="Pulse vs Node ws · Python websockets — same machine (asyncio clients)",
        rows=msg_rows,
        higher_better=True,
    )
    write_bar_chart(
        path=ASSETS / "benchmark_pulse_latency.svg",
        title="WebSocket echo P99 latency (ms)",
        subtitle="Pulse vs Node ws · Python websockets — same machine (asyncio clients)",
        rows=lat_rows,
        higher_better=False,
    )


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--duration", type=float, default=6.0)
    ap.add_argument("--clients", type=int, default=64)
    ap.add_argument("--payload", default="x" * 64)
    ap.add_argument("--skip-build", action="store_true")
    args = ap.parse_args()

    pulse_bin = BENCH / "servers" / "socketify_pulse"
    if not pulse_bin.exists():
        print(f"missing {pulse_bin} — run ./benchmarks/run_pulse.sh first", file=sys.stderr)
        sys.exit(1)

    targets = [
        {
            "name": "Pulse (Socketify)",
            "cmd": [str(pulse_bin), "19180"],
            "url": "ws://127.0.0.1:19180/pong",
        },
        {
            "name": "ws (Node.js)",
            "cmd": ["node", str(BENCH / "servers" / "ws_node_echo.js"), "19181"],
            "url": "ws://127.0.0.1:19181/pong",
        },
        {
            "name": "websockets (Python)",
            "cmd": [sys.executable, str(BENCH / "servers" / "ws_python_echo.py"), "19182"],
            "url": "ws://127.0.0.1:19182",
        },
    ]

    env = os.environ.copy()
    sr = ROOT / ".deps" / "sysroot" / "usr"
    if sr.exists():
        env["LD_LIBRARY_PATH"] = (
            f"{sr}/lib/x86_64-linux-gnu:{sr}/lib:" + env.get("LD_LIBRARY_PATH", "")
        )

    results: list[dict] = []
    for t in targets:
        print(f"==> {t['name']}", flush=True)
        p = start_proc(t["cmd"], env if "socketify_pulse" in t["cmd"][0] else None)
        try:
            time.sleep(0.35)
            stats = asyncio.run(run_load(t["url"], args.clients, args.duration, args.payload))
            row = {"name": t["name"], **stats}
            results.append(row)
            print(
                f"    {stats['msgs_per_sec']:,.0f} msg/s  "
                f"p50={stats['latency_p50_ms']:.3f} ms  p99={stats['latency_p99_ms']:.3f} ms",
                flush=True,
            )
        finally:
            stop_proc(p)

    out_json = {
        "meta": {
            "tool": "asyncio + websockets client",
            "duration_s": args.duration,
            "clients": args.clients,
            "payload_bytes": len(args.payload),
            "endpoint": "WebSocket text echo (round-trip)",
            "notes": (
                "Pulse speaks RFC 6455 — browsers and wscat work unchanged. "
                "This compares Socketify Pulse echo throughput against popular WS stacks."
            ),
        },
        "results": results,
    }
    (BENCH / "pulse_results.json").write_text(json.dumps(out_json, indent=2) + "\n")
    write_csv(BENCH / "pulse_results.csv", results)
    render_charts(results)
    print(f"wrote {BENCH / 'pulse_results.csv'}")
    print(f"wrote {BENCH / 'pulse_results.json'}")


if __name__ == "__main__":
    main()
