#pragma once
// socketify/detail/file_io.h — File I/O operations for sending files

#include "socket.h" // for NativeSocket
#include <string_view>
#include <optional>

namespace socketify::detail {

// Metadata about a file
struct FileInfo {
    size_t size;
    // Could add modification time, etc. later
};

// Gets metadata for a file.
// Returns nullopt if file doesn't exist or isn't a regular file.
std::optional<FileInfo> get_file_info(std::string_view path);

// Sends a file over a socket using sendfile() if available.
// This is a high-performance way to transfer file data.
//
// - socket_fd: The target socket.
// - file_path: The path to the file on the filesystem.
//
// Returns true on success, false on error.
bool send_file(NativeSocket socket_fd, std::string_view file_path);

} // namespace socketify::detail