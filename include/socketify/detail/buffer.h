#pragma once
// socketify/detail/buffer.h — A simple read/write buffer

#include <vector>
#include <string>
#include <string_view>
#include <cstddef>

namespace socketify::detail {

// A simple buffer for reading data from a socket.
// It's basically a wrapper around a std::vector<char> that
// keeps track of read and write positions.
class Buffer {
public:
    explicit Buffer(size_t initial_size = 4096);

    // Get a view of the readable data.
    std::string_view view() const;

    // How many bytes are available to read.
    size_t readable_bytes() const;

    // Get a pointer to the start of the writable area.
    char* write_ptr();

    // How many bytes can be written without reallocating.
    size_t writable_bytes() const;

    // Mark `n` bytes as having been written.
    void produced(size_t n);

    // Mark `n` bytes as having been read.
    void consumed(size_t n);

    // Ensure the buffer has at least `n` writable bytes.
    void ensure_writable(size_t n);

    // Read data from a file descriptor into the buffer.
    // Returns bytes read, 0 on EOF, -1 on error.
    ssize_t read_from_fd(int fd);

private:
    std::vector<char> storage_;
    size_t read_pos_{0};
    size_t write_pos_{0};
};

} // namespace socketify::detail