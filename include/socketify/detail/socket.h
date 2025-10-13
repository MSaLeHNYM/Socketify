#pragma once
// socketify/detail/socket.h — Low-level socket operations

#include <cstdint>
#include <string_view>
#include <optional>
#include <unistd.h>

// Use file descriptor for POSIX, SOCKET for Windows
#if defined(_WIN32)
  using NativeSocket = uintptr_t; // SOCKET
  const NativeSocket kInvalidSocket = ~static_cast<NativeSocket>(0);
#else
  using NativeSocket = int;
  const NativeSocket kInvalidSocket = -1;
#endif


namespace socketify::detail {

// Creates a listening socket.
// Returns the file descriptor or kInvalidSocket on error.
NativeSocket create_listening_socket(std::string_view host, uint16_t port, int backlog);

// Accepts a new connection.
// Returns the client socket or kInvalidSocket on error.
NativeSocket accept_connection(NativeSocket listen_fd);

// Closes a socket.
void close_socket(NativeSocket fd);

// Sets a socket to be non-blocking.
bool set_non_blocking(NativeSocket fd);

// Reads data from a socket.
// Returns bytes read, 0 on disconnect, -1 on error (check errno).
ssize_t read_socket(NativeSocket fd, void* buf, size_t count);

// Writes data to a socket.
// Returns bytes written, or -1 on error.
ssize_t write_socket(NativeSocket fd, const void* buf, size_t count);

// Writes all data to a socket (handles partial writes).
// Returns true on success, false on error.
bool write_all(NativeSocket fd, std::string_view data);

} // namespace socketify::detail