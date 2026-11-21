#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace mango::cli
{

// Linux: one end of a control connection (see ControlServer.hpp), text
// lines over a Unix socket. One thread reads; any thread may write.
class LineSocket
{
   public:
    explicit LineSocket(int fd);
    ~LineSocket();

    LineSocket(const LineSocket&) = delete;
    LineSocket& operator=(const LineSocket&) = delete;

    // The next line, without its newline. nullopt once the other end closed
    // (or Shutdown was called).
    [[nodiscard]] std::optional<std::string> ReadLine();

    // line plus a newline, whole. false once the other end is gone.
    bool WriteLine(std::string_view line);

    // Ends the connection both ways: a ReadLine blocked on either end
    // returns nullopt.
    void Shutdown();

   private:
    bool FillBuffer();

    int m_fd;
    std::string m_buffer;
    std::mutex m_writeMutex;
};

// $XDG_RUNTIME_DIR/mango/control.sock (made with 0700), the same for every
// login and SSH session of this user. Without XDG_RUNTIME_DIR: control.sock
// in the data folder.
[[nodiscard]] std::filesystem::path ControlSocketPath();

// nullptr when no Mango listens there: no socket file, or a stale one left
// by a Mango that's gone.
[[nodiscard]] std::unique_ptr<LineSocket> ConnectControlSocket(const std::filesystem::path& path);

// The listening socket's fd, readable only by this user. Replaces a socket
// file already there, so only call it holding the InstanceLock.
[[nodiscard]] std::expected<int, std::string> ListenOnControlSocket(const std::filesystem::path& path);

// Whether the process on the other end of a connection runs as this user.
[[nodiscard]] bool IsSameUser(int fd);

}  // namespace mango::cli
