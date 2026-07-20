#!/usr/bin/env python3
"""Start each ping server, hammer /ping with wrk, write results + SVG charts."""
from __future__ import annotations

import argparse
import json
import os
import re
import signal
import subprocess
import sys
import time
from pathlib import Path
from urllib.request import urlopen


def wait_ready(url: str, timeout: float = 20.0) -> None:
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        try:
            with urlopen(url, timeout=1.0) as r:
                if r.status == 200:
                    return
        except Exception as e:  # noqa: BLE001
            last = e
            time.sleep(0.05)
    raise RuntimeError(f"server not ready at {url}: {last}")


def parse_wrk(stdout: str) -> dict:
    # Requests/sec:  900505.31
    # Latency Distribution / 50% 55.00us / 99% 126.00us
    # Running ... Latency Avg Stdev Max
    rps_m = re.search(r"Requests/sec:\s*([0-9.]+)", stdout)
    transfer_m = re.search(r"Transfer/sec:\s*([0-9.]+)(\w+)", stdout)
    req_m = re.search(r"(\d+)\s+requests in", stdout)
    lat_avg_m = re.search(
        r"Latency\s+([0-9.]+)(us|ms|s)\s+([0-9.]+)(us|ms|s)\s+([0-9.]+)(us|ms|s)",
        stdout,
    )
    p50_m = re.search(r"50%\s+([0-9.]+)(us|ms|s)", stdout)
    p99_m = re.search(r"99%\s+([0-9.]+)(us|ms|s)", stdout)

    def to_ms(val: str, unit: str) -> float:
        v = float(val)
        if unit == "us":
            return v / 1000.0
        if unit == "s":
            return v * 1000.0
        return v

    out = {
        "rps": float(rps_m.group(1)) if rps_m else 0.0,
        "requests_ok": int(req_m.group(1)) if req_m else 0,
        "latency_avg_ms": to_ms(lat_avg_m.group(1), lat_avg_m.group(2)) if lat_avg_m else 0.0,
        "latency_p50_ms": to_ms(p50_m.group(1), p50_m.group(2)) if p50_m else 0.0,
        "latency_p99_ms": to_ms(p99_m.group(1), p99_m.group(2)) if p99_m else 0.0,
        "raw": stdout,
    }
    if transfer_m:
        out["transfer_per_sec"] = transfer_m.group(1) + transfer_m.group(2)
    return out


def run_wrk(wrk: Path, url: str, duration: int, connections: int, threads: int) -> dict:
    cmd = [
        str(wrk),
        f"-t{threads}",
        f"-c{connections}",
        f"-d{duration}s",
        "--latency",
        url,
    ]
    p = subprocess.run(cmd, capture_output=True, text=True, check=False)
    text = p.stdout + "\n" + p.stderr
    if p.returncode != 0 and "Requests/sec" not in text:
        raise RuntimeError(f"wrk failed ({p.returncode}): {text[-400:]}")
    return parse_wrk(text)


def kill_proc(proc: subprocess.Popen) -> None:
    if proc.poll() is not None:
        return
    try:
        os.killpg(proc.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGKILL)
        proc.wait(timeout=2)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--duration", type=int, default=8)
    ap.add_argument("--concurrency", type=int, default=100)
    ap.add_argument("--threads", type=int, default=4)
    ap.add_argument("--port-base", type=int, default=19080)
    args = ap.parse_args()

    root = Path(args.root)
    bench = root / "benchmarks"
    wrk = bench / "wrk"
    if not wrk.is_file():
        print("error: benchmarks/wrk missing — build wrk first", file=sys.stderr)
        return 1

    venv_py = bench / ".venv" / "bin" / "python"
    sock_bin = bench / "servers" / "socketify_ping"

    servers = [
        {
            "name": "Socketify",
            "port": args.port_base,
            "cmd": [str(sock_bin), str(args.port_base)],
            "cwd": str(bench / "servers"),
        },
        {
            "name": "Flask (Waitress)",
            "port": args.port_base + 1,
            "cmd": [str(venv_py), str(bench / "servers" / "flask_ping.py"), str(args.port_base + 1)],
            "cwd": str(bench / "servers"),
        },
        {
            "name": "Express (Node)",
            "port": args.port_base + 2,
            "cmd": ["node", str(bench / "servers" / "express_ping.js"), str(args.port_base + 2)],
            "cwd": str(bench),
        },
        {
            "name": "Django (Waitress)",
            "port": args.port_base + 3,
            "cmd": [str(venv_py), str(bench / "servers" / "django_ping.py"), str(args.port_base + 3)],
            "cwd": str(bench / "servers"),
        },
    ]

    results: list[dict] = []
    meta = {
        "tool": "wrk",
        "duration_s": args.duration,
        "connections": args.concurrency,
        "threads": args.threads,
        "endpoint": "GET /ping → {\"ok\":true}",
        "host": "127.0.0.1",
        "cpu_cores": os.cpu_count(),
        "notes": (
            "Same machine, localhost, keep-alive. Minimal handlers (Django with MIDDLEWARE=[]). "
            "Not identical to TechEmpower/Sharkbench hardware — use for relative comparison."
        ),
    }

    for s in servers:
        url = f"http://127.0.0.1:{s['port']}/ping"
        print(f"==> starting {s['name']} on :{s['port']}", flush=True)
        proc = subprocess.Popen(
            s["cmd"],
            cwd=s["cwd"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            start_new_session=True,
        )
        try:
            wait_ready(url)
            print(
                f"    wrk -t{args.threads} -c{args.concurrency} -d{args.duration}s {url}",
                flush=True,
            )
            stats = run_wrk(wrk, url, args.duration, args.concurrency, args.threads)
            row = {"name": s["name"], **{k: v for k, v in stats.items() if k != "raw"}}
            results.append(row)
            print(
                f"    → {row['rps']:,.0f} req/s  "
                f"avg={row['latency_avg_ms']:.3f}ms  p99={row['latency_p99_ms']:.3f}ms",
                flush=True,
            )
        except Exception as e:  # noqa: BLE001
            err = ""
            try:
                err = proc.stderr.read().decode()[-600:]
            except Exception:
                pass
            print(f"    FAILED: {e}\n{err}", file=sys.stderr)
            results.append(
                {
                    "name": s["name"],
                    "rps": 0.0,
                    "latency_avg_ms": 0.0,
                    "latency_p50_ms": 0.0,
                    "latency_p99_ms": 0.0,
                    "error": str(e),
                }
            )
        finally:
            kill_proc(proc)

    by_rps = sorted(results, key=lambda r: r.get("rps", 0), reverse=True)
    out_json = bench / "results.json"
    out_json.write_text(json.dumps({"meta": meta, "results": by_rps}, indent=2) + "\n")
    print(f"\nwrote {out_json}")

    # Theme-adaptive animated SVGs (transparent background)
    render = bench / "render_charts.py"
    subprocess.run([sys.executable, str(render)], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
