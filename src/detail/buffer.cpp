#include "socketify/detail/buffer.h"
#include "socketify/detail/socket.h" // for read_socket

#include <algorithm> // for std::copy
#include <stdexcept>

namespace socketify::detail {

Buffer::Buffer(size_t initial_size) : storage_(initial_size) {}

std::string_view Buffer::view() const {
    return {storage_.data() + read_pos_, readable_bytes()};
}

size_t Buffer::readable_bytes() const {
    return write_pos_ - read_pos_;
}

char* Buffer::write_ptr() {
    return storage_.data() + write_pos_;
}

size_t Buffer::writable_bytes() const {
    return storage_.size() - write_pos_;
}

void Buffer::produced(size_t n) {
    if (n > writable_bytes()) {
        throw std::length_error("Produced more bytes than writable space");
    }
    write_pos_ += n;
}

void Buffer::consumed(size_t n) {
    if (n > readable_bytes()) {
        throw std::length_error("Consumed more bytes than readable");
    }
    read_pos_ += n;

    if (read_pos_ == write_pos_) {
        // Buffer is empty, reset positions
        read_pos_ = 0;
        write_pos_ = 0;
    }
}

void Buffer::ensure_writable(size_t n) {
    if (writable_bytes() >= n) {
        return;
    }

    // If there's free space at the beginning, move data to make room
    if (read_pos_ > 0) {
        size_t readable = readable_bytes();
        std::copy(storage_.begin() + read_pos_, storage_.begin() + write_pos_, storage_.begin());
        read_pos_ = 0;
        write_pos_ = readable;
    }

    // If still not enough space, reallocate
    if (writable_bytes() < n) {
        storage_.resize(write_pos_ + n);
    }
}

ssize_t Buffer::read_from_fd(int fd) {
    ensure_writable(1); // Ensure there's at least 1 byte of space
    ssize_t bytes_read = read_socket(fd, write_ptr(), writable_bytes());
    if (bytes_read > 0) {
        produced(bytes_read);
    }
    return bytes_read;
}

} // namespace socketify::detail