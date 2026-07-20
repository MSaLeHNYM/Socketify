// 10_pulse_chat — bidirectional lobby chat over Pulse (RFC 6455 WebSocket).
//
// Keep the connection pulsing. Open two browser tabs, pick names, and talk.
// The server uses pulse::Hub rooms for fan-out; browsers use native WebSocket.
//
// Try it:
//   open http://localhost:8080 in two tabs
//   wscat -c ws://localhost:8080/chat

#include <socketify/socketify.h>

#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>

using namespace socketify;

static const char* kPage = R"html(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Pulse lobby</title>
<style>
  :root {
    --ink: #1a2332;
    --muted: #5c6b7a;
    --line: #c5d0db;
    --paper: #f2f6f9;
    --accent: #0d7377;
    --accent-ink: #fff;
    --sys: #3d5a80;
  }
  * { box-sizing: border-box; }
  body {
    margin: 0; min-height: 100vh;
    font-family: "Segoe UI", "IBM Plex Sans", sans-serif;
    color: var(--ink);
    background:
      radial-gradient(ellipse 80% 50% at 10% -10%, #b8e0d2 0%, transparent 55%),
      radial-gradient(ellipse 60% 40% at 100% 0%, #d6e4f0 0%, transparent 50%),
      var(--paper);
  }
  main {
    max-width: 36rem; margin: 0 auto; padding: 2.5rem 1.25rem 3rem;
  }
  h1 {
    font-family: "Iowan Old Style", "Palatino Linotype", Palatino, serif;
    font-weight: 600; font-size: 2.1rem; letter-spacing: -0.02em;
    margin: 0 0 0.35rem;
  }
  .tag { color: var(--muted); font-size: 0.95rem; margin-bottom: 1.5rem; }
  #gate, #room { display: flex; flex-direction: column; gap: 0.75rem; }
  #room { display: none; }
  label { font-size: 0.8rem; color: var(--muted); text-transform: uppercase; letter-spacing: 0.04em; }
  input {
    width: 100%; padding: 0.65rem 0.75rem; border: 1px solid var(--line);
    border-radius: 6px; font: inherit; background: #fff;
  }
  button {
    padding: 0.65rem 1rem; border: 0; border-radius: 6px;
    background: var(--accent); color: var(--accent-ink); font: inherit; cursor: pointer;
  }
  button:disabled { opacity: 0.5; cursor: default; }
  #status { font-size: 0.85rem; color: var(--sys); min-height: 1.2em; }
  #log {
    height: 18rem; overflow-y: auto; padding: 0.85rem 0;
    border-top: 1px solid var(--line); border-bottom: 1px solid var(--line);
    display: flex; flex-direction: column; gap: 0.45rem;
  }
  .msg { line-height: 1.35; }
  .msg .u { font-weight: 600; color: var(--accent); margin-right: 0.4rem; }
  .msg.sys { color: var(--sys); font-size: 0.9rem; font-style: italic; }
  form.row { display: flex; gap: 0.5rem; }
  form.row input { flex: 1; }
</style>
</head><body>
<main>
  <h1>Pulse</h1>
  <p class="tag">Keep the connection pulsing — lobby chat over WebSocket.</p>

  <div id="gate">
    <label for="name">Display name</label>
    <input id="name" placeholder="alex" maxlength="24" autocomplete="nickname">
    <button id="join" type="button">Join lobby</button>
  </div>

  <div id="room">
    <div id="status">connecting…</div>
    <div id="log"></div>
    <form class="row" id="f">
      <input id="m" placeholder="message" autocomplete="off" disabled>
      <button id="send" disabled>Send</button>
    </form>
  </div>
</main>
<script>
(() => {
  const gate = document.getElementById('gate');
  const room = document.getElementById('room');
  const logEl = document.getElementById('log');
  const status = document.getElementById('status');
  const nameIn = document.getElementById('name');
  const msgIn = document.getElementById('m');
  const sendBtn = document.getElementById('send');
  let ws = null;
  let me = '';

  const line = (html, cls) => {
    const d = document.createElement('div');
    d.className = 'msg' + (cls ? ' ' + cls : '');
    d.innerHTML = html;
    logEl.appendChild(d);
    logEl.scrollTop = logEl.scrollHeight;
  };
  const esc = s => String(s).replace(/[&<>"']/g, c =>
    ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));

  document.getElementById('join').onclick = () => {
    me = (nameIn.value || '').trim() || 'guest';
    gate.style.display = 'none';
    room.style.display = 'flex';
    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    ws = new WebSocket(proto + '//' + location.host + '/chat');
    ws.onopen = () => {
      status.textContent = 'connected · lobby';
      msgIn.disabled = false;
      sendBtn.disabled = false;
      ws.send(JSON.stringify({ type: 'join', user: me }));
      msgIn.focus();
    };
    ws.onmessage = ev => {
      let o;
      try { o = JSON.parse(ev.data); } catch { line(esc(ev.data), 'sys'); return; }
      if (o.type === 'chat') line('<span class="u">' + esc(o.user) + '</span>' + esc(o.text));
      else if (o.type === 'sys') line(esc(o.text), 'sys');
      else if (o.type === 'welcome') line(esc(o.text || 'welcome'), 'sys');
    };
    ws.onclose = () => {
      status.textContent = 'disconnected';
      msgIn.disabled = true;
      sendBtn.disabled = true;
    };
  };

  document.getElementById('f').onsubmit = e => {
    e.preventDefault();
    const text = msgIn.value.trim();
    if (!text || !ws || ws.readyState !== 1) return;
    ws.send(JSON.stringify({ type: 'chat', user: me, text }));
    msgIn.value = '';
  };
})();
</script>
</body></html>)html";

int main() {
    Server server;
    pulse::Hub hub;
    std::mutex names_mu;
    std::unordered_map<void*, std::string> names; // Channel::Impl* → display name

    server.Get("/", [](Request&, Response& res) { res.html(kPage); });

    server.Get("/chat", [&](Request& req, Response& res) {
        auto ch = pulse::upgrade(req, res);
        if (!ch.valid()) return;

        hub.join("lobby", ch);
        ch.send_text(R"({"type":"welcome","text":"welcome to the lobby — say hello"})");

        ch.on_text([&hub, &names_mu, &names](pulse::Channel& c, std::string_view msg) {
            // Minimal JSON peek without a full parser dependency in the demo.
            auto find_str = [&](std::string_view key) -> std::string {
                const std::string needle = "\"" + std::string(key) + "\":\"";
                auto p = msg.find(needle);
                if (p == std::string_view::npos) return {};
                p += needle.size();
                auto e = msg.find('"', p);
                if (e == std::string_view::npos) return {};
                return std::string(msg.substr(p, e - p));
            };

            const auto type = find_str("type");
            if (type == "join") {
                auto user = find_str("user");
                if (user.empty()) user = "guest";
                {
                    std::lock_guard<std::mutex> lk(names_mu);
                    names[c.impl().get()] = user;
                }
                const auto note =
                    std::string(R"({"type":"sys","text":")") + user + " joined\"}";
                hub.to("lobby").broadcast_text(note);
                return;
            }
            if (type == "chat") {
                auto user = find_str("user");
                auto text = find_str("text");
                if (text.empty()) return;
                if (user.empty()) {
                    std::lock_guard<std::mutex> lk(names_mu);
                    auto it = names.find(c.impl().get());
                    user = it != names.end() ? it->second : "anon";
                }
                // Escape quotes in text for a tiny JSON envelope.
                std::string safe;
                safe.reserve(text.size());
                for (char chv : text) {
                    if (chv == '"' || chv == '\\') safe.push_back('\\');
                    if (chv == '\n') { safe += "\\n"; continue; }
                    safe.push_back(chv);
                }
                const auto out = std::string(R"({"type":"chat","user":")") + user +
                                 R"(","text":")" + safe + "\"}";
                hub.to("lobby").broadcast_text(out);
            }
        });

        ch.on_close([&hub, &names_mu, &names](pulse::Channel& c, pulse::CloseCode,
                                              std::string_view) {
            std::string user;
            {
                std::lock_guard<std::mutex> lk(names_mu);
                auto it = names.find(c.impl().get());
                if (it != names.end()) {
                    user = it->second;
                    names.erase(it);
                }
            }
            hub.leave_all(c);
            if (!user.empty()) {
                hub.to("lobby").broadcast_text(
                    std::string(R"({"type":"sys","text":")") + user + " left\"}");
            }
        });
    });

    if (!server.Run("0.0.0.0", 8080)) {
        std::fprintf(stderr, "failed to start: %s\n", server.last_error().c_str());
        return 1;
    }
    std::printf("Pulse chat on http://localhost:8080  (ws://localhost:8080/chat)\n");
    server.Wait();
    return 0;
}
