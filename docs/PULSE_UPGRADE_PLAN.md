# Pulse Protocol Upgrade Plan

## Goals

1. **Bigger core** — production-grade WebSocket engine with streaming, backpressure, and performance work
2. **Dev-friendly wrapper** (`pulse_easy`) — JSON events, rooms, auto-cleanup, minimal boilerplate
3. **Media wrapper** (`pulse_media`) — voice, video, and image streaming over Pulse binary frames
4. **Speed** — faster unmask, encode-once broadcast, zero-copy paths where safe, no extra copies in server I/O

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│  pulse_media  — voice / video / image relay API         │
├─────────────────────────────────────────────────────────┤
│  pulse_easy   — App, Connection, JSON emit/on, routes   │
├─────────────────────────────────────────────────────────┤
│  pulse (core) — RFC 6455, Channel, Hub, fragmentation   │
└─────────────────────────────────────────────────────────┘
```

## Phase 1 — Core (`pulse.h`, `pulse.cpp`, `pulse_impl.h`, `server.cpp`)

### Performance
- Fast XOR unmask (8-byte chunks) in `decode_frame`
- `Channel::send_raw()` — enqueue pre-encoded frames (Hub broadcast encodes once)
- `feed_bytes` fed directly from connection buffer (no interim `std::string` copy)
- Move-based `enqueue(std::string&&)` to avoid redundant copies

### Streaming / fragmentation
- Outbound: `begin_text` / `write_text` / `end_text` (and binary variants)
- `send_text_stream` / `send_binary_stream` — auto-chunk large payloads per `Options::fragment_size`
- Inbound fragmentation unchanged (already supported)

### Backpressure & observability
- `Options::max_pending_bytes` (default 4 MiB)
- `Channel::pending_bytes()`, `writable()`, `id()`
- `on_ping` / `on_pong` handlers

### Hub
- `broadcast_frame(room, encoded)` — fan-out without re-encoding
- `prune(room)` — drop dead channels
- `members(room)` — snapshot of live channels

## Phase 2 — Easy wrapper (`pulse_easy.h`, `pulse_easy.cpp`)

```cpp
pulse_easy::App app;
app.on("/chat", [](pulse_easy::Connection& conn) {
    conn.join("lobby");
    conn.emit("welcome", {{"text", "hello"}});
    conn.on("chat", [&](auto& c, const json& msg) {
        c.broadcast("lobby", "chat", msg);
    });
});
app.bind(server);  // registers GET /chat → upgrade + handler
```

- JSON message envelope: `{ "type": "...", "data": { ... } }`
- Auto `hub.leave_all` on close
- `conn.emit(type, data)`, `conn.on(type, fn)`, `conn.broadcast(room, type, data)`
- Optional shared `pulse::Hub` injection

## Phase 3 — Media wrapper (`pulse_media.h`, `pulse_media.cpp`)

Binary application protocol (on Pulse binary opcode):

| Offset | Size | Field |
|--------|------|-------|
| 0 | 2 | Magic `PM` |
| 2 | 1 | Kind: Voice=1, Video=2, Image=3, ImageEnd=4 |
| 3 | 2 | Stream ID |
| 5 | 4 | Sequence |
| 9 | 8 | Timestamp (µs) |
| 17 | 1 | Flags (keyframe, last, …) |
| 18 | 2 | MIME length (Image only) |
| 20 | n | MIME string (Image only) |
| … | … | Payload |

```cpp
pulse_media::Hub media(easy_app.hub());
media.on_voice("call-1", [](Channel& from, auto& frame) { ... });
media.send_voice("call-1", pcm_bytes);
media.send_video("call-1", h264_nalu, /*keyframe=*/true);
media.send_image("call-1", jpeg_bytes, "image/jpeg");
```

- `MediaHub` attaches to channels, routes by room
- Chunked image upload via `begin_image` / `write_image` / `end_image`
- Voice/video use sequence + timestamp for jitter buffers on client

## Phase 4 — Wiring

- CMake public headers + sources
- `#include <socketify/pulse_easy.h>` / `pulse_media.h` in umbrella
- Unit tests: fragmentation, backpressure, media pack/unpack, easy JSON routing
- Integration test: media relay round-trip
- Update `examples/10_pulse_chat` or add `11_pulse_media_demo`
- `docs/API.md` sections

## Success criteria

- All existing Pulse tests pass
- New tests green (target +20 tests)
- Example demonstrates text chat + optional voice/image hooks
- API usable from `#include <socketify/socketify.h>`
