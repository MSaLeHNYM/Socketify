#pragma once
/**
 * @file loop.h
 * @brief Minimal epoll-based event loop used by each worker thread.
 *
 * The loop multiplexes socket readiness, supports cross-thread wakeups via
 * eventfd (used by SSE broadcasts), and tracks per-connection deadlines with
 * a monotonic-clock deadline heap.
 */

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace socketify::detail {

/** @brief Readiness event delivered by EventLoop::wait(). */
struct LoopEvent {
    void* data{nullptr};   ///< User pointer registered with add()/mod().
    bool readable{false};  ///< EPOLLIN (or error/hup, reported as readable).
    bool writable{false};  ///< EPOLLOUT.
    bool error{false};     ///< EPOLLERR / EPOLLHUP / EPOLLRDHUP.
};

/**
 * @brief Thin epoll wrapper with an eventfd wakeup channel.
 *
 * Not thread-safe except for wakeup() and post(), which may be called from
 * any thread.
 */
class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    /** @brief True when epoll and eventfd were created successfully. */
    bool valid() const noexcept { return epfd_ >= 0; }

    /**
     * @brief Register @p fd.
     * @param read  Subscribe to readability.
     * @param write Subscribe to writability.
     * @param data  Opaque pointer returned in LoopEvent::data.
     */
    bool add(int fd, bool read, bool write, void* data);

    /** @brief Update interest set for a registered fd. */
    bool mod(int fd, bool read, bool write, void* data);

    /** @brief Remove @p fd from the interest set. */
    bool del(int fd);

    /**
     * @brief Wait for events.
     * @param out        Receives ready events (cleared first).
     * @param timeout_ms Max wait; -1 blocks indefinitely.
     * @return Number of events, 0 on timeout, -1 on error.
     */
    int wait(std::vector<LoopEvent>& out, int timeout_ms);

    /** @brief Wake the loop from another thread. Safe to call anytime. */
    void wakeup();

    /**
     * @brief Queue a callback to run on the loop thread on the next
     *        iteration, then wake the loop. Thread-safe.
     */
    void post(std::function<void()> fn);

    /** @brief Run queued post() callbacks. Called by the loop owner. */
    void run_posted();

private:
    int epfd_{-1};
    int wake_fd_{-1};

    std::mutex posted_mu_;
    std::vector<std::function<void()>> posted_;
};

} // namespace socketify::detail
