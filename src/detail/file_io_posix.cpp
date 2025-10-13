#include "socketify/detail/file_io.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/sendfile.h>
#endif

namespace socketify::detail {

std::optional<FileInfo> get_file_info(std::string_view path) {
    struct stat file_stat;
    if (stat(std::string(path).c_str(), &file_stat) != 0) {
        return std::nullopt;
    }
    if (!S_ISREG(file_stat.st_mode)) {
        return std::nullopt; // Not a regular file
    }
    return FileInfo{static_cast<size_t>(file_stat.st_size)};
}


bool send_file(NativeSocket socket_fd, std::string_view file_path) {
    int file_fd = open(std::string(file_path).c_str(), O_RDONLY);
    if (file_fd < 0) {
        return false;
    }

    auto info = get_file_info(file_path);
    if (!info) {
        close(file_fd);
        return false;
    }
    off_t size = static_cast<off_t>(info->size);
    off_t offset = 0;

#if defined(__linux__)
    // Use sendfile on Linux - most efficient
    ssize_t sent = sendfile(socket_fd, file_fd, &offset, size);
    close(file_fd);
    return sent == static_cast<ssize_t>(size);

#elif defined(__APPLE__)
    // Use sendfile on macOS
    off_t len = size;
    int result = sendfile(file_fd, socket_fd, 0, &len, nullptr, 0);
    close(file_fd);
    return result == 0 && len == size;

#else
    // Fallback for other POSIX systems: manual read/write loop
    char buffer[8192];
    ssize_t bytes_read;
    bool success = true;
    while ((bytes_read = read(file_fd, buffer, sizeof(buffer))) > 0) {
        if (!write_all(socket_fd, std::string_view(buffer, bytes_read))) {
            success = false;
            break;
        }
    }
    if (bytes_read < 0) {
        success = false;
    }
    close(file_fd);
    return success;
#endif
}

} // namespace socketify::detail