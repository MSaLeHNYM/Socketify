# Benchmarks

## HTTP `/ping`

Same-machine comparison of **Socketify** vs **Flask**, **Express**, and **Django**
on a minimal JSON `GET /ping` → `{"ok":true}` endpoint.

### Run

```bash
./benchmarks/run_all.sh
```

Requires: CMake/C++, Python 3 (venv), Node.js/npm, network once (to fetch wrk
sources if `benchmarks/wrk` is missing).

Outputs:

- `benchmarks/results.json` — raw numbers
- `assets/benchmark_rps.svg` — animated throughput chart (transparent, theme-adaptive)
- `assets/benchmark_latency.svg` — animated p99 latency chart (transparent, theme-adaptive)

Re-render charts only:

```bash
python3 benchmarks/render_charts.py
```

### Servers

| Name | File | Notes |
|---|---|---|
| Socketify | `servers/socketify_ping.cpp` | Release, all CPU workers |
| Flask | `servers/flask_ping.py` | Waitress, 32 threads |
| Express | `servers/express_ping.js` | Express 4 |
| Django | `servers/django_ping.py` | Waitress, `MIDDLEWARE=[]` |

Load tool: **wrk** (`-t4 -c100` by default).

## Pulse (WebSocket echo + Hub fan-out)

Pulse speaks RFC 6455 — same wire protocol as browser `WebSocket`. This suite
compares Socketify Pulse echo against Node `ws` and Python `websockets`, plus an
in-process Hub encode-once fan-out microbench.

### Run

```bash
./benchmarks/run_pulse.sh
# optional: DURATION=8 CLIENTS=128 ./benchmarks/run_pulse.sh
./benchmarks/servers/pulse_hub_fanout 1000 2000
```

Outputs:

- `benchmarks/pulse_results.csv` / `pulse_results.json` — echo numbers
- `benchmarks/pulse_hub_results.csv` — Hub fan-out numbers
- `assets/benchmark_pulse_msgs.svg` — animated echo throughput
- `assets/benchmark_pulse_latency.svg` — animated echo p99
- `assets/benchmark_pulse_hub.svg` — animated Hub fan-out

### Servers

| Name | File | Notes |
|---|---|---|
| Pulse | `servers/socketify_pulse.cpp` | `/pong` echo + `/echo` Hub |
| Node `ws` | `servers/ws_node_echo.js` | `ws@8` |
| Python | `servers/ws_python_echo.py` | `websockets` |
| Hub microbench | `servers/pulse_hub_fanout.cpp` | encode-once vs per-peer |
