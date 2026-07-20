// 05_sse_chat — live message feed over Server-Sent Events.
//
// A background thread broadcasts a clock tick every 2 seconds, and any
// client can POST a message that is pushed to every connected browser.
//
// Try it:
//   open http://localhost:8080 in two browser tabs
//   curl -X POST http://localhost:8080/say -d 'hello from curl'

#include <socketify/socketify.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

using namespace socketify;

// Fan-out hub: keeps live SSE sessions, prunes dead ones on broadcast.
class Hub {
public:
    void add(sse::Session s) {
        std::lock_guard<std::mutex> lk(mu_);
        sessions_.push_back(std::move(s));
    }

    void broadcast(std::string_view event, std::string_view data) {
        std::lock_guard<std::mutex> lk(mu_);
        std::erase_if(sessions_, [&](sse::Session& s) {
            return !s.send_event(event, data);
        });
    }

    std::size_t size() {
        std::lock_guard<std::mutex> lk(mu_);
        return sessions_.size();
    }

private:
    std::mutex mu_;
    std::vector<sse::Session> sessions_;
};

static const char* kPage = R"html(<!doctype html>
<html><head><meta charset="utf-8"><title>SSE chat</title>
<style>
 body{font-family:system-ui;max-width:40rem;margin:2rem auto;padding:0 1rem}
 #log{border:1px solid #8884;border-radius:8px;padding:1rem;height:16rem;overflow-y:auto}
 form{display:flex;gap:.5rem;margin-top:1rem}
 input{flex:1;padding:.5rem}
</style></head><body>
<h1>Socketify SSE chat</h1>
<div id="log"></div>
<form id="f"><input id="m" placeholder="say something" autocomplete="off"><button>Send</button></form>
<script>
 const log = t => { const d=document.createElement('div'); d.textContent=t;
   const el=document.getElementById('log'); el.appendChild(d); el.scrollTop=el.scrollHeight; };
 const es = new EventSource('/events');
 es.addEventListener('chat', e => log('> ' + e.data));
 es.addEventListener('tick', e => log('[tick] ' + e.data));
 document.getElementById('f').onsubmit = async e => {
   e.preventDefault();
   const m = document.getElementById('m');
   await fetch('/say', {method:'POST', body: m.value});
   m.value='';
 };
</script></body></html>)html";

int main() {
    Server server;
    Hub hub;

    server.Get("/", [](Request&, Response& res) { res.html(kPage); });

    server.Get("/events", [&](Request& req, Response& res) {
        auto s = sse::upgrade(req, res);
        s.send_event("chat", "welcome! clients online: " + std::to_string(hub.size() + 1));
        hub.add(std::move(s));
    });

    server.Post("/say", [&](Request& req, Response& res) {
        hub.broadcast("chat", req.body_view());
        res.status(Status::NoContent).end();
    });

    if (!server.Run("0.0.0.0", 8080)) {
        std::fprintf(stderr, "failed to start: %s\n", server.last_error().c_str());
        return 1;
    }
    std::printf("SSE chat on http://localhost:8080\n");

    // Broadcast a tick from a plain background thread — SSE sessions are
    // thread-safe handles.
    std::atomic<bool> running{true};
    std::thread ticker([&] {
        int n = 0;
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            hub.broadcast("tick", std::to_string(++n));
        }
    });

    server.Wait();
    running = false;
    ticker.join();
    return 0;
}
