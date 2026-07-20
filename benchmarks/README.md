# Benchmarks

Same-machine comparison of **Socketify** vs **Flask**, **Express**, and **Django**
on a minimal JSON `GET /ping` → `{"ok":true}` endpoint.

## Run

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

## Servers

| Name | File | Notes |
|---|---|---|
| Socketify | `servers/socketify_ping.cpp` | Release, all CPU workers |
| Flask | `servers/flask_ping.py` | Waitress, 32 threads |
| Express | `servers/express_ping.js` | Express 4 |
| Django | `servers/django_ping.py` | Waitress, `MIDDLEWARE=[]` |

Load tool: **wrk** (`-t4 -c100` by default).
