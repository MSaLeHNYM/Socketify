# Nexus Board — Socketify + React showcase

A full collaborative **project / task board** demonstrating nearly every
Socketify feature, with a polished React (Vite) frontend and **SQLite** storage.

## Features exercised

| Area | What you get |
|---|---|
| Routing / groups | `/api/auth`, `/api/projects`, `/api/tasks`, … |
| Sessions | Signed cookie `nexus_sid`, login state |
| Auth | Register / login / logout + `require_auth` middleware |
| SQLite | Users, projects, tasks, comments (WAL mode) |
| Body parsers | JSON APIs + **multipart** avatar upload |
| Cookies | Theme cookie on login + session cookie |
| CORS | Credentialed + reflected Origin (Vite dev proxy friendly) |
| Rate limit | Global + stricter on auth routes |
| Logging / request id | Request logs + `X-Request-Id` |
| Compression | gzip/deflate on larger responses |
| Static files | React `dist/` + `/uploads` avatars |
| SSE | Live project/task/comment events |
| `send_file` | JSON export download |
| SPA fallback | Client-side React Router |

## Quick start

### 1. Build the React UI

```bash
cd examples/08_nexus_board/frontend
npm install
npm run build
```

### 2. Build & run the Socketify server

```bash
cmake -S . -B build -DSOCKETIFY_BUILD_EXAMPLES=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc) --target example_08_nexus_board
cd build/examples/08_nexus_board
./example_08_nexus_board
```

Open **http://localhost:8080** — sign up, create a project, drag tasks across
columns (via status buttons), comment, upload an avatar. Open a second browser
tab to see **SSE** updates land live.

Or: `./scripts/run_examples.sh 08` (after the frontend has been built once).

### Dev mode (hot reload UI)

Terminal A — API:

```bash
./example_08_nexus_board   # still serves API on :8080
```

Terminal B — Vite:

```bash
cd frontend && npm run dev   # http://localhost:5173 (proxies /api)
```

## API map

- `POST /api/auth/register|login` · `POST /api/auth/logout` · `GET /api/auth/me`
- `GET/POST /api/projects` · `GET/DELETE /api/projects/:id`
- `GET/POST /api/projects/:id/tasks` · `PATCH/DELETE /api/tasks/:id`
- `GET/POST /api/tasks/:id/comments`
- `PATCH /api/profile` · `POST /api/profile/avatar` (multipart)
- `GET /api/stats` · `GET /api/export/projects.json` · `GET /api/events` (SSE)
- `GET /api/health`
