/**
 * @file pulse.cpp
 * @brief Pulse realtime channels: RFC 6455 handshake, framing, Hub.
 */

#include "socketify/pulse.h"
#include "socketify/detail/pulse_impl.h"
#include "socketify/detail/utils.h"

#include <algorithm>
#include <algorithm>
#include <cstring>

namespace socketify::pulse {
namespace {

constexpr char kGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

bool header_has_token_(std::string_view header, std::string_view token) {
    // Split on commas, trim, case-insensitive compare
    std::size_t start = 0;
    while (start <= header.size()) {
        auto pos = header.find(',', start);
        auto part = (pos == std::string_view::npos) ? header.substr(start)
                                                    : header.substr(start, pos - start);
        part = detail::trim_view(part);
        if (detail::iequal_ascii(part, token)) return true;
        if (pos == std::string_view::npos) break;
        start = pos + 1;
    }
    return false;
}

std::string pick_subprotocol_(std::string_view client_list,
                              const std::vector<std::string>& wanted) {
    if (wanted.empty() || client_list.empty()) return {};
    std::size_t start = 0;
    while (start <= client_list.size()) {
        auto pos = client_list.find(',', start);
        auto part = (pos == std::string_view::npos) ? client_list.substr(start)
                                                    : client_list.substr(start, pos - start);
        part = detail::trim_view(part);
        for (const auto& w : wanted) {
            if (detail::iequal_ascii(part, w)) return w;
        }
        if (pos == std::string_view::npos) break;
        start = pos + 1;
    }
    return {};
}

} // namespace

std::string accept_key(std::string_view client_key) {
    std::string material(client_key);
    material.append(kGuid);
    auto dig = detail::sha1(material);
    return detail::base64_encode(dig.data(), dig.size());
}

std::string encode_frame(std::uint8_t opcode, std::string_view payload, bool fin) {
    std::string out;
    out.reserve(2 + 8 + payload.size());
    unsigned char b0 = static_cast<unsigned char>((fin ? 0x80 : 0x00) | (opcode & 0x0f));
    out.push_back(static_cast<char>(b0));
    // server → client: mask bit = 0
    if (payload.size() < 126) {
        out.push_back(static_cast<char>(payload.size()));
    } else if (payload.size() <= 0xffff) {
        out.push_back(126);
        out.push_back(static_cast<char>((payload.size() >> 8) & 0xff));
        out.push_back(static_cast<char>(payload.size() & 0xff));
    } else {
        out.push_back(127);
        std::uint64_t n = payload.size();
        for (int i = 7; i >= 0; --i) out.push_back(static_cast<char>((n >> (8 * i)) & 0xff));
    }
    out.append(payload.data(), payload.size());
    return out;
}

DecodedFrame decode_frame(std::string_view data, std::size_t max_payload) {
    DecodedFrame f;
    if (data.size() < 2) return f;
    auto b0 = static_cast<unsigned char>(data[0]);
    auto b1 = static_cast<unsigned char>(data[1]);
    f.fin = (b0 & 0x80) != 0;
    f.opcode = b0 & 0x0f;
    bool masked = (b1 & 0x80) != 0;
    std::uint64_t len = b1 & 0x7f;
    std::size_t off = 2;
    if (len == 126) {
        if (data.size() < 4) return f;
        len = (std::uint64_t(static_cast<unsigned char>(data[2])) << 8) |
              std::uint64_t(static_cast<unsigned char>(data[3]));
        off = 4;
    } else if (len == 127) {
        if (data.size() < 10) return f;
        len = 0;
        for (int i = 0; i < 8; ++i)
            len = (len << 8) | static_cast<unsigned char>(data[2 + i]);
        off = 10;
        // Reject reserved top bit / absurd sizes early
        if (len >> 63) {
            f.protocol_error = true;
            return f;
        }
    }
    if (len > max_payload) {
        f.protocol_error = true;
        return f;
    }
    if (!masked) {
        // RFC: client→server MUST be masked. Treat as protocol error.
        f.protocol_error = true;
        return f;
    }
    if (data.size() < off + 4 + len) return f;
    unsigned char mask[4];
    for (int i = 0; i < 4; ++i) mask[i] = static_cast<unsigned char>(data[off + i]);
    off += 4;
    f.payload.resize(static_cast<std::size_t>(len));
    for (std::size_t i = 0; i < len; ++i)
        f.payload[i] = static_cast<char>(static_cast<unsigned char>(data[off + i]) ^ mask[i % 4]);
    f.bytes_consumed = off + static_cast<std::size_t>(len);
    f.ok = true;
    return f;
}

bool feed_bytes(const std::shared_ptr<Channel::Impl>& impl, std::string_view data) {
    if (!impl) return false;
    TextHandler on_text;
    BinaryHandler on_binary;
    CloseHandler on_close;
    PingHandler on_ping;
    PongHandler on_pong;
    Options opts;
    {
        std::lock_guard<std::mutex> lk(impl->mu);
        if (impl->closed) return false;
        impl->inbuf.append(data.data(), data.size());
        on_text = impl->on_text;
        on_binary = impl->on_binary;
        on_close = impl->on_close;
        on_ping = impl->on_ping;
        on_pong = impl->on_pong;
        opts = impl->opts;
    }

    Channel ch(impl);
    while (true) {
        DecodedFrame fr;
        {
            std::lock_guard<std::mutex> lk(impl->mu);
            fr = decode_frame(impl->inbuf, opts.max_message_bytes);
            if (fr.protocol_error) {
                impl->closed = true;
                return false;
            }
            if (!fr.ok) return true;
            impl->inbuf.erase(0, fr.bytes_consumed);
        }

        switch (fr.opcode) {
            case 0x0: { // continuation
                std::string msg;
                std::uint8_t base_op = 0;
                {
                    std::lock_guard<std::mutex> lk(impl->mu);
                    impl->fragment.append(fr.payload);
                    if (!fr.fin) break;
                    msg = std::move(impl->fragment);
                    base_op = impl->fragment_opcode;
                    impl->fragment.clear();
                    impl->fragment_opcode = 0;
                }
                if (base_op == 0x1 && on_text) on_text(ch, msg);
                else if (base_op == 0x2 && on_binary) on_binary(ch, msg);
                break;
            }
            case 0x1: // text
            case 0x2: { // binary
                if (!fr.fin) {
                    std::lock_guard<std::mutex> lk(impl->mu);
                    impl->fragment = std::move(fr.payload);
                    impl->fragment_opcode = fr.opcode;
                    break;
                }
                if (fr.opcode == 0x1 && on_text) on_text(ch, fr.payload);
                else if (fr.opcode == 0x2 && on_binary) on_binary(ch, fr.payload);
                break;
            }
            case 0x8: { // close
                CloseCode code = CloseCode::NoStatus;
                std::string_view reason;
                if (fr.payload.size() >= 2) {
                    code = static_cast<CloseCode>(
                        (static_cast<unsigned char>(fr.payload[0]) << 8) |
                        static_cast<unsigned char>(fr.payload[1]));
                    reason = std::string_view(fr.payload.data() + 2, fr.payload.size() - 2);
                }
                // Echo close
                ch.close(code, reason);
                bool fire = false;
                CloseHandler cb;
                {
                    std::lock_guard<std::mutex> lk(impl->mu);
                    if (!impl->close_fired) {
                        impl->close_fired = true;
                        fire = true;
                        cb = impl->on_close;
                    }
                }
                if (fire && cb) cb(ch, code, reason);
                return true;
            }
            case 0x9: { // ping
                if (on_ping) on_ping(ch, fr.payload);
                if (opts.auto_pong) ch.pong(fr.payload);
                break;
            }
            case 0xA: // pong
                if (on_pong) on_pong(ch, fr.payload);
                break;
            default:
                return false;
        }
    }
}

// ---- Channel methods ----

bool Channel::alive() const {
    if (!impl_) return false;
    std::lock_guard<std::mutex> lk(impl_->mu);
    return !impl_->closed;
}

std::uint64_t Channel::id() const {
    if (!impl_) return 0;
    return impl_->id;
}

std::size_t Channel::pending_bytes() const {
    if (!impl_) return 0;
    std::lock_guard<std::mutex> lk(impl_->mu);
    return impl_->pending.size();
}

bool Channel::writable() const {
    if (!impl_) return false;
    std::lock_guard<std::mutex> lk(impl_->mu);
    if (impl_->closed) return false;
    return impl_->pending.size() < impl_->opts.max_pending_bytes;
}

bool Channel::begin_fragment_(std::uint8_t opcode) {
    if (!impl_) return false;
    std::lock_guard<std::mutex> lk(impl_->mu);
    if (impl_->closed || impl_->out_frag_active) return false;
    impl_->out_frag_active = true;
    impl_->out_frag_opcode = opcode;
    impl_->out_frag_first = true;
    return true;
}

bool Channel::write_fragment_(std::string_view chunk, bool fin) {
    if (!impl_) return false;
    std::uint8_t base_opcode = 0;
    bool first = false;
    {
        std::lock_guard<std::mutex> lk(impl_->mu);
        if (!impl_->out_frag_active) return false;
        base_opcode = impl_->out_frag_opcode;
        first = impl_->out_frag_first;
        if (first) impl_->out_frag_first = false;
        if (fin) {
            impl_->out_frag_active = false;
            impl_->out_frag_opcode = 0;
        }
    }
    return impl_->enqueue(encode_frame(first ? base_opcode : 0x0, chunk, fin));
}

bool Channel::begin_text() { return begin_fragment_(0x1); }
bool Channel::write_text(std::string_view chunk) { return write_fragment_(chunk, false); }
bool Channel::end_text() { return write_fragment_({}, true); }
bool Channel::begin_binary() { return begin_fragment_(0x2); }
bool Channel::write_binary(std::string_view chunk) { return write_fragment_(chunk, false); }
bool Channel::end_binary() { return write_fragment_({}, true); }

bool Channel::send_raw(std::string_view encoded_frame) {
    if (!impl_) return false;
    return impl_->enqueue(encoded_frame);
}

bool Channel::send_text_stream(std::string_view data) {
    if (!impl_) return false;
    std::size_t chunk = impl_->opts.fragment_size;
    if (data.size() <= chunk) return send_text(data);
    if (!begin_text()) return false;
    for (std::size_t off = 0; off < data.size();) {
        const auto take = std::min(chunk, data.size() - off);
        const bool fin = (off + take >= data.size());
        if (!write_fragment_(data.substr(off, take), fin)) return false;
        off += take;
    }
    return true;
}

bool Channel::send_binary_stream(std::string_view data) {
    if (!impl_) return false;
    std::size_t chunk = impl_->opts.fragment_size;
    if (data.size() <= chunk) return send_binary(data);
    if (!begin_binary()) return false;
    for (std::size_t off = 0; off < data.size();) {
        const auto take = std::min(chunk, data.size() - off);
        const bool fin = (off + take >= data.size());
        if (!write_fragment_(data.substr(off, take), fin)) return false;
        off += take;
    }
    return true;
}

bool Channel::send_text(std::string_view data) {
    if (!impl_) return false;
    return impl_->enqueue(encode_frame(0x1, data));
}

bool Channel::send_binary(std::string_view data) {
    if (!impl_) return false;
    return impl_->enqueue(encode_frame(0x2, data));
}

bool Channel::ping(std::string_view payload) {
    if (!impl_) return false;
    return impl_->enqueue(encode_frame(0x9, payload));
}

bool Channel::pong(std::string_view payload) {
    if (!impl_) return false;
    return impl_->enqueue(encode_frame(0xA, payload));
}

void Channel::close(CloseCode code, std::string_view reason) {
    if (!impl_) return;
    std::string payload;
    payload.push_back(static_cast<char>((static_cast<std::uint16_t>(code) >> 8) & 0xff));
    payload.push_back(static_cast<char>(static_cast<std::uint16_t>(code) & 0xff));
    payload.append(reason.data(), reason.size());
    {
        std::lock_guard<std::mutex> lk(impl_->mu);
        if (impl_->closed) return;
        impl_->close_requested = true;
    }
    impl_->enqueue(encode_frame(0x8, payload));
}

void Channel::on_text(TextHandler fn) {
    if (!impl_) return;
    std::lock_guard<std::mutex> lk(impl_->mu);
    impl_->on_text = std::move(fn);
}

void Channel::on_binary(BinaryHandler fn) {
    if (!impl_) return;
    std::lock_guard<std::mutex> lk(impl_->mu);
    impl_->on_binary = std::move(fn);
}

void Channel::on_close(CloseHandler fn) {
    if (!impl_) return;
    std::lock_guard<std::mutex> lk(impl_->mu);
    impl_->on_close = std::move(fn);
}

void Channel::on_ping(PingHandler fn) {
    if (!impl_) return;
    std::lock_guard<std::mutex> lk(impl_->mu);
    impl_->on_ping = std::move(fn);
}

void Channel::on_pong(PongHandler fn) {
    if (!impl_) return;
    std::lock_guard<std::mutex> lk(impl_->mu);
    impl_->on_pong = std::move(fn);
}

const std::string& Channel::protocol() const {
    static const std::string empty;
    if (!impl_) return empty;
    return impl_->protocol;
}

Channel upgrade(Request& req, Response& res, Options opts) {
    auto upgrade_h = req.header("Upgrade");
    auto conn_h = req.header("Connection");
    auto key = req.header("Sec-WebSocket-Key");
    auto ver = req.header("Sec-WebSocket-Version");

    if (!detail::iequal_ascii(upgrade_h, "websocket") ||
        !header_has_token_(conn_h, "Upgrade") || key.empty() ||
        (!ver.empty() && ver != "13")) {
        res.status(Status::BadRequest).send("Pulse upgrade failed\n");
        return {};
    }

    auto impl = std::make_shared<Channel::Impl>();
    impl->opts = opts;
    impl->self = impl;
    auto proto = pick_subprotocol_(req.header("Sec-WebSocket-Protocol"), opts.subprotocols);
    impl->protocol = proto;

    res.status(Status::SwitchingProtocols)
        .set_header("Upgrade", "websocket")
        .set_header("Connection", "Upgrade")
        .set_header("Sec-WebSocket-Accept", accept_key(key));
    if (!proto.empty()) res.set_header("Sec-WebSocket-Protocol", proto);
    res.mark_pulse(impl);

    return Channel(impl);
}

// ---- Hub ----

void Hub::join(std::string room, Channel ch) {
    if (!ch.valid()) return;
    std::lock_guard<std::mutex> lk(mu_);
    auto& v = rooms_[std::move(room)];
    v.push_back(std::move(ch));
}

void Hub::leave(std::string room, const Channel& ch) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = rooms_.find(room);
    if (it == rooms_.end()) return;
    auto& v = it->second;
    auto* a = ch.impl().get();
    v.erase(std::remove_if(v.begin(), v.end(),
                           [&](const Channel& c) { return c.impl().get() == a; }),
            v.end());
    if (v.empty()) rooms_.erase(it);
}

void Hub::leave_all(const Channel& ch) {
    std::lock_guard<std::mutex> lk(mu_);
    auto* a = ch.impl().get();
    for (auto it = rooms_.begin(); it != rooms_.end();) {
        auto& v = it->second;
        v.erase(std::remove_if(v.begin(), v.end(),
                               [&](const Channel& c) { return c.impl().get() == a; }),
                v.end());
        if (v.empty()) it = rooms_.erase(it);
        else ++it;
    }
}

void Hub::broadcast_text(std::string_view room, std::string_view data) {
    broadcast_frame(room, encode_frame(0x1, data));
}

void Hub::broadcast_binary(std::string_view room, std::string_view data) {
    broadcast_frame(room, encode_frame(0x2, data));
}

void Hub::broadcast_frame(std::string_view room, std::string_view encoded_frame) {
    std::vector<Channel> snap;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = rooms_.find(std::string(room));
        if (it == rooms_.end()) return;
        snap = it->second;
    }
    for (auto& c : snap) c.send_raw(encoded_frame);
}

void Hub::broadcast_text(std::string_view data) {
    std::vector<Channel> snap;
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& [_, members] : rooms_)
            for (auto& c : members) snap.push_back(c);
    }
    for (auto& c : snap) c.send_text(data);
}

std::size_t Hub::room_size(std::string_view room) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = rooms_.find(std::string(room));
    return it == rooms_.end() ? 0 : it->second.size();
}

std::size_t Hub::prune(std::string_view room) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = rooms_.find(std::string(room));
    if (it == rooms_.end()) return 0;
    const auto before = it->second.size();
    auto& v = it->second;
    v.erase(std::remove_if(v.begin(), v.end(), [](const Channel& c) { return !c.alive(); }),
            v.end());
    if (v.empty()) rooms_.erase(it);
    return before - v.size();
}

std::vector<Channel> Hub::members(std::string_view room) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = rooms_.find(std::string(room));
    if (it == rooms_.end()) return {};
    return it->second;
}

} // namespace socketify::pulse
