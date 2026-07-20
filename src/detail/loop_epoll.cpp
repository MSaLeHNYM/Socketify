/**
 * @file loop_epoll.cpp
 * @brief epoll(7) implementation of detail::EventLoop.
 */

#include "socketify/detail/loop.h"

#include <cerrno>
#include <cstdint>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace socketify::detail {

EventLoop::EventLoop() {
    epfd_ = ::epoll_create1(EPOLL_CLOEXEC);
    wake_fd_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (epfd_ >= 0 && wake_fd_ >= 0) {
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.ptr = nullptr; // nullptr marks the wakeup channel
        ::epoll_ctl(epfd_, EPOLL_CTL_ADD, wake_fd_, &ev);
    }
}

EventLoop::~EventLoop() {
    if (wake_fd_ >= 0) ::close(wake_fd_);
    if (epfd_ >= 0) ::close(epfd_);
}

static std::uint32_t make_events_(bool read, bool write) {
    std::uint32_t ev = EPOLLRDHUP;
    if (read) ev |= EPOLLIN;
    if (write) ev |= EPOLLOUT;
    return ev;
}

bool EventLoop::add(int fd, bool read, bool write, void* data) {
    epoll_event ev{};
    ev.events = make_events_(read, write);
    ev.data.ptr = data;
    return ::epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) == 0;
}

bool EventLoop::mod(int fd, bool read, bool write, void* data) {
    epoll_event ev{};
    ev.events = make_events_(read, write);
    ev.data.ptr = data;
    return ::epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev) == 0;
}

bool EventLoop::del(int fd) {
    return ::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr) == 0;
}

int EventLoop::wait(std::vector<LoopEvent>& out, int timeout_ms) {
    out.clear();
    epoll_event evs[256];
    int n = ::epoll_wait(epfd_, evs, 256, timeout_ms);
    if (n < 0) {
        if (errno == EINTR) return 0;
        return -1;
    }
    out.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        if (evs[i].data.ptr == nullptr) {
            // Drain the wakeup eventfd.
            std::uint64_t v;
            while (::read(wake_fd_, &v, sizeof(v)) > 0) {}
            continue;
        }
        LoopEvent le;
        le.data = evs[i].data.ptr;
        le.readable = (evs[i].events & EPOLLIN) != 0;
        le.writable = (evs[i].events & EPOLLOUT) != 0;
        le.error = (evs[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0;
        out.push_back(le);
    }
    return static_cast<int>(out.size());
}

void EventLoop::wakeup() {
    std::uint64_t one = 1;
    [[maybe_unused]] ssize_t rc = ::write(wake_fd_, &one, sizeof(one));
}

void EventLoop::post(std::function<void()> fn) {
    {
        std::lock_guard<std::mutex> lk(posted_mu_);
        posted_.push_back(std::move(fn));
    }
    wakeup();
}

void EventLoop::run_posted() {
    std::vector<std::function<void()>> fns;
    {
        std::lock_guard<std::mutex> lk(posted_mu_);
        fns.swap(posted_);
    }
    for (auto& fn : fns) fn();
}

} // namespace socketify::detail
