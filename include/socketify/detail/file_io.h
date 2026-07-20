#pragma once
/**
 * @file file_io.h
 * @brief File helpers used for zero-copy static file / send_file streaming.
 */

#include <cstdint>
#include <string>
#include <string_view>

namespace socketify::detail {

/**
 * @brief RAII descriptor for a regular file opened for streaming.
 *
 * Used by the server's writer to drive sendfile(2) without loading the
 * whole file into memory.
 */
class FileHandle {
public:
    FileHandle() = default;
    ~FileHandle() { close(); }

    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    FileHandle(FileHandle&& o) noexcept : fd_(o.fd_), size_(o.size_), mtime_(o.mtime_) { o.fd_ = -1; }
    FileHandle& operator=(FileHandle&& o) noexcept {
        if (this != &o) {
            close();
            fd_ = o.fd_; size_ = o.size_; mtime_ = o.mtime_;
            o.fd_ = -1;
        }
        return *this;
    }

    /**
     * @brief Open @p path read-only. Fails for non-regular files.
     * @return true on success.
     */
    bool open(std::string_view path);

    /** @brief Close the descriptor. Idempotent. */
    void close() noexcept;

    /** @brief Underlying fd, or -1. */
    int fd() const noexcept { return fd_; }
    /** @brief True while open. */
    bool valid() const noexcept { return fd_ >= 0; }
    /** @brief File size in bytes at open time. */
    std::uint64_t size() const noexcept { return size_; }
    /** @brief Modification time (unix seconds) at open time. */
    std::int64_t mtime() const noexcept { return mtime_; }

private:
    int fd_{-1};
    std::uint64_t size_{0};
    std::int64_t mtime_{0};
};

/**
 * @brief Read a byte range of a file into @p out.
 * @return false on open/read failure.
 */
bool read_file_range(std::string_view path, std::uint64_t offset, std::uint64_t len, std::string& out);

} // namespace socketify::detail
