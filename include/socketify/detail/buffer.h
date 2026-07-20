#pragma once
/**
 * @file buffer.h
 * @brief Growable byte buffer used for socket input/output staging.
 *
 * The buffer keeps a single contiguous allocation with a read cursor, so
 * consuming from the front is O(1) and the storage is compacted lazily.
 */

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

namespace socketify::detail {

/**
 * @brief FIFO byte buffer with cheap front-consumption.
 *
 * Producers call append(); consumers call data()/size() then consume(n).
 * Memory is reclaimed automatically once the read cursor grows large.
 */
class Buffer {
public:
    Buffer() = default;

    /** @brief Append raw bytes to the end of the buffer. */
    void append(const char* p, std::size_t n) {
        if (n == 0) return;
        maybe_compact_();
        storage_.append(p, n);
    }

    /** @brief Append a string view. */
    void append(std::string_view sv) { append(sv.data(), sv.size()); }

    /** @brief Pointer to the first unread byte. */
    const char* data() const noexcept { return storage_.data() + head_; }

    /** @brief Number of unread bytes. */
    std::size_t size() const noexcept { return storage_.size() - head_; }

    /** @brief True when there are no unread bytes. */
    bool empty() const noexcept { return size() == 0; }

    /** @brief View over the unread bytes. */
    std::string_view view() const noexcept { return {data(), size()}; }

    /**
     * @brief Mark @p n bytes as consumed (they will no longer be visible).
     * Consuming more than size() clamps to size().
     */
    void consume(std::size_t n) {
        head_ += (n > size()) ? size() : n;
        if (head_ == storage_.size()) {
            storage_.clear();
            head_ = 0;
        }
    }

    /** @brief Drop all content. */
    void clear() noexcept {
        storage_.clear();
        head_ = 0;
    }

private:
    void maybe_compact_() {
        // Compact when the dead prefix dominates the allocation.
        if (head_ > 4096 && head_ > storage_.size() / 2) {
            storage_.erase(0, head_);
            head_ = 0;
        }
    }

    std::string storage_;
    std::size_t head_{0};
};

} // namespace socketify::detail
