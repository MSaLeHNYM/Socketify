// In-process Pulse Hub fan-out microbench (no sockets).
// Compares N× encode (per-peer send_text) vs encode-once broadcast_frame.
#include <socketify/pulse.h>
#include <socketify/detail/pulse_impl.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace socketify;
using Steady = std::chrono::steady_clock;

static double ms_since(Steady::time_point t0) {
    return std::chrono::duration<double, std::milli>(Steady::now() - t0).count();
}

static void drain(std::vector<pulse::Channel>& chs) {
    for (auto& c : chs) {
        auto impl = c.impl();
        std::lock_guard<std::mutex> lk(impl->mu);
        impl->pending.clear();
    }
}

int main(int argc, char** argv) {
    const int peers = argc > 1 ? std::atoi(argv[1]) : 1000;
    const int rounds = argc > 2 ? std::atoi(argv[2]) : 2000;
    const std::string payload(256, 'p');

    std::vector<pulse::Channel> chs;
    chs.reserve(peers);
    for (int i = 0; i < peers; ++i)
        chs.emplace_back(std::make_shared<pulse::Channel::Impl>());

    pulse::Hub hub;
    for (auto& c : chs) hub.join("room", c);

    // Warm + drain
    for (int i = 0; i < 50; ++i)
        for (auto& c : chs) c.send_text(payload);
    drain(chs);

    auto t0 = Steady::now();
    for (int i = 0; i < rounds; ++i)
        for (auto& c : chs) c.send_text(payload);
    const double per_peer_ms = ms_since(t0);
    const double per_peer_msg = double(rounds) * peers / (per_peer_ms / 1000.0);
    drain(chs);

    t0 = Steady::now();
    for (int i = 0; i < rounds; ++i) {
        auto frame = pulse::encode_frame(0x1, payload);
        hub.broadcast_frame("room", frame);
    }
    const double once_ms = ms_since(t0);
    const double once_msg = double(rounds) * peers / (once_ms / 1000.0);

    std::printf("peers=%d rounds=%d payload=%zu\n", peers, rounds, payload.size());
    std::printf("per_peer_send_text:    %.0f delivered-msg/s  (%.1f ms)\n", per_peer_msg, per_peer_ms);
    std::printf("hub_broadcast_frame:   %.0f delivered-msg/s  (%.1f ms)\n", once_msg, once_ms);
    std::printf("speedup:               %.2fx\n", once_msg / per_peer_msg);

    // Append CSV row for README tooling
    std::printf("CSV,Pulse Hub encode-once,%.0f,%.0f,%.2f\n", once_msg, per_peer_msg,
                once_msg / per_peer_msg);
    return 0;
}
