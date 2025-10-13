#include "socketify/sse.h"
#include "socketify/detail/utils.h" // for detail::write_all

#include <fmt/core.h>

namespace socketify {

namespace {
    // Newlines in data must be sent as separate "data:" lines.
    void write_data_lines(std::string& buf, std::string_view data) {
        size_t start = 0;
        while (start < data.size()) {
            size_t end = data.find('\n', start);
            if (end == std::string_view::npos) {
                buf.append("data: ");
                buf.append(data.substr(start));
                buf.push_back('\n');
                break;
            }
            buf.append("data: ");
            buf.append(data.substr(start, end - start));
            buf.push_back('\n');
            start = end + 1;
        }
    }
}

SSE::SSE(Response& res) : res_(res) {
    if (!res.ended()) {
        res.set_content_type("text/event-stream; charset=utf-8");
        res.set_header("Cache-Control", "no-cache");
        res.set_header("Connection", "keep-alive");
    } else {
        closed_ = true;
    }
}

bool SSE::send(std::string_view data) {
    if (closed_) return false;

    std::string buf;
    write_data_lines(buf, data);
    buf.push_back('\n'); // end of message
    return write_chunk_(buf);
}

bool SSE::send_event(std::string_view event_name, std::string_view data) {
    if (closed_ || event_name.find('\n') != std::string_view::npos) {
        return false;
    }

    std::string buf;
    buf.append("event: ");
    buf.append(event_name);
    buf.push_back('\n');
    write_data_lines(buf, data);
    buf.push_back('\n'); // end of message
    return write_chunk_(buf);
}

bool SSE::send_comment(std::string_view comment) {
    if (closed_ || comment.find('\n') != std::string_view::npos) {
        return false;
    }

    std::string buf;
    buf.push_back(':'); // comment line
    buf.append(comment);
    buf.push_back('\n');
    buf.push_back('\n'); // end of message
    return write_chunk_(buf);
}

bool SSE::set_retry(unsigned int ms) {
    if (closed_) return false;
    auto buf = fmt::format("retry: {}\n\n", ms);
    return write_chunk_(buf);
}

void SSE::close() {
    if (!closed_.exchange(true)) {
        res_.end();
    }
}

bool SSE::write_chunk_(std::string_view chunk) {
    if (closed_) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) return false; // re-check after lock
    bool ok = res_.write(chunk);
    if (!ok) {
        closed_ = true;
    }
    return ok;
}

} // namespace socketify