/**
 * @file sse.cpp
 * @brief Server-Sent Events session formatting and upgrade.
 */

#include "socketify/sse.h"
#include "socketify/detail/sse_impl.h"

namespace socketify::sse {

// Format a payload as one or more "data:" lines (newlines split the payload).
static void append_data_lines_(std::string& out, std::string_view data) {
    std::size_t start = 0;
    while (true) {
        std::size_t nl = data.find('\n', start);
        std::string_view line = (nl == std::string_view::npos)
                                    ? data.substr(start)
                                    : data.substr(start, nl - start);
        out.append("data: ");
        out.append(line);
        out.push_back('\n');
        if (nl == std::string_view::npos) break;
        start = nl + 1;
    }
}

bool Session::send(std::string_view data) {
    if (!impl_) return false;
    std::string msg;
    msg.reserve(data.size() + 16);
    append_data_lines_(msg, data);
    msg.push_back('\n');
    return impl_->enqueue(msg);
}

bool Session::send_event(std::string_view event, std::string_view data, std::string_view id) {
    if (!impl_) return false;
    std::string msg;
    msg.reserve(event.size() + data.size() + id.size() + 32);
    if (!id.empty()) {
        msg.append("id: ").append(id).push_back('\n');
    }
    if (!event.empty()) {
        msg.append("event: ").append(event).push_back('\n');
    }
    append_data_lines_(msg, data);
    msg.push_back('\n');
    return impl_->enqueue(msg);
}

bool Session::comment(std::string_view text) {
    if (!impl_) return false;
    std::string msg;
    msg.reserve(text.size() + 4);
    msg.append(": ").append(text).append("\n\n");
    return impl_->enqueue(msg);
}

void Session::close() {
    if (!impl_) return;
    std::function<void()> n;
    {
        std::lock_guard<std::mutex> lk(impl_->mu);
        if (impl_->closed) return;
        impl_->close_requested = true;
        n = impl_->notify;
    }
    if (n) n();
}

bool Session::alive() const {
    if (!impl_) return false;
    std::lock_guard<std::mutex> lk(impl_->mu);
    return !impl_->closed;
}

Session upgrade(Request& /*req*/, Response& res) {
    auto impl = std::make_shared<Session::Impl>();

    res.status(Status::OK)
        .set_header(H_ContentType, "text/event-stream")
        .set_header(H_CacheControl, "no-cache")
        .set_header("X-Accel-Buffering", "no");
    res.mark_stream(impl);

    return Session(impl);
}

} // namespace socketify::sse
