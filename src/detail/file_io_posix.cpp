/**
 * @file file_io_posix.cpp
 * @brief POSIX implementation of detail::FileHandle.
 */

#include "socketify/detail/file_io.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace socketify::detail {

bool FileHandle::open(std::string_view path) {
    close();
    std::string p(path);
    int fd = ::open(p.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;

    struct stat st{};
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        ::close(fd);
        return false;
    }
    fd_ = fd;
    size_ = static_cast<std::uint64_t>(st.st_size);
    mtime_ = static_cast<std::int64_t>(st.st_mtime);
    return true;
}

void FileHandle::close() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    size_ = 0;
    mtime_ = 0;
}

bool read_file_range(std::string_view path, std::uint64_t offset, std::uint64_t len, std::string& out) {
    FileHandle fh;
    if (!fh.open(path)) return false;
    out.resize(static_cast<std::size_t>(len));
    std::size_t got = 0;
    while (got < len) {
        ssize_t rc = ::pread(fh.fd(), &out[got], static_cast<std::size_t>(len - got),
                             static_cast<off_t>(offset + got));
        if (rc < 0) return false;
        if (rc == 0) break;
        got += static_cast<std::size_t>(rc);
    }
    out.resize(got);
    return true;
}

} // namespace socketify::detail
