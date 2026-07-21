// 11_pulse_media — Pulse Easy JSON chat + pulse_media voice/image relay.
//
//   http://localhost:8080        — browser UI
//   ws://localhost:8080/ws       — JSON chat (pulse_easy)
//   Binary PM frames on same socket relay voice/image to the room.

#include <socketify/socketify.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>

using namespace socketify;

int main() {
    Server server;
    pulse_easy::App app;
    pulse_media::Hub media(&app.hub());

    std::mutex mu;
    std::atomic<std::size_t> voice_frames{0};
    std::atomic<std::size_t> image_bytes{0};

    media.on_voice("lobby", [&](pulse::Channel&, const pulse_media::Frame& f) {
        ++voice_frames;
        media.send_voice("lobby", f.payload, f.stream_id);
    });

    media.on_image("lobby", [&](pulse::Channel&, const pulse_media::Frame& f) {
        if (f.kind == pulse_media::Kind::ImageEnd) return;
        image_bytes += f.payload.size();
        media.send_image("lobby", f.payload, f.mime);
    });

    app.on("/ws", [&](pulse_easy::Connection& conn) {
        conn.join("lobby");
        media.join("lobby", conn.channel());
        conn.emit("welcome", {{"text", "Pulse Easy + Media demo"}});
        conn.on("chat", [](pulse_easy::Connection& c, const pulse_easy::json& msg) {
            c.broadcast("lobby", "chat", msg);
        });
    });
    app.bind(server);

    server.Get("/", [](Request&, Response& res) {
        res.html(R"html(<!doctype html><html><head><meta charset=utf-8>
<title>Pulse Media</title></head><body>
<h1>Pulse Easy + Media</h1>
<p>Open devtools → Network → WS on <code>/ws</code>.</p>
<p>Send JSON chat: <code>{"type":"chat","data":{"text":"hi"}}</code></p>
<p>Send binary PM voice/image frames via <code>pulse_media::pack</code> from a client.</p>
</body></html>)html");
    });

    server.Get("/stats", [&](Request&, Response& res) {
        res.json({{"voice_frames", voice_frames.load()}, {"image_bytes", image_bytes.load()}});
    });

    if (!server.Run("0.0.0.0", 8080)) {
        std::fprintf(stderr, "failed: %s\n", server.last_error().c_str());
        return 1;
    }
    std::printf("Pulse media demo http://localhost:8080\n");
    server.Wait();
    return 0;
}
