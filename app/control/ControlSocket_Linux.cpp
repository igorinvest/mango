#include "control/ControlSocket.hpp"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <format>

#include "file/File.hpp"

namespace fs = std::filesystem;

namespace mango::cli
{

namespace
{

constexpr int kListenBacklog = 8;

std::string ErrnoText(const char* what) { return std::format("{}: {}", what, std::strerror(errno)); }

// sun_path holds about 108 bytes; a longer path can't be used at all.
std::optional<sockaddr_un> SocketAddress(const fs::path& path)
{
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const auto& text = path.native();
    if (text.size() >= sizeof(address.sun_path))
    {
        return std::nullopt;
    }
    std::memcpy(address.sun_path, text.c_str(), text.size() + 1);
    return address;
}

int NewSocket() { return socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0); }

// The runtime folder exists while the user's systemd manager runs (with
// lingering, without a login too). Only this user may enter mango/ in it.
std::optional<fs::path> RuntimeFolder()
{
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (runtime == nullptr || *runtime == '\0')
    {
        return std::nullopt;
    }
    const auto folder = fs::path(runtime) / "mango";
    if (mkdir(folder.c_str(), 0700) != 0 && errno != EEXIST)
    {
        return std::nullopt;
    }
    return folder;
}

std::expected<int, std::string> BindAndListen(const sockaddr_un& address, const fs::path& path)
{
    const int fd = NewSocket();
    if (fd < 0)
    {
        return std::unexpected(ErrnoText("socket"));
    }
    if (bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
    {
        const auto error = ErrnoText("bind");
        close(fd);
        return std::unexpected(error);
    }
    chmod(path.c_str(), 0600);
    if (listen(fd, kListenBacklog) != 0)
    {
        const auto error = ErrnoText("listen");
        close(fd);
        return std::unexpected(error);
    }
    return fd;
}

}  // namespace

LineSocket::LineSocket(int fd) : m_fd(fd) {}

LineSocket::~LineSocket() { close(m_fd); }

std::optional<std::string> LineSocket::ReadLine()
{
    auto newline = m_buffer.find('\n');
    while (newline == std::string::npos)
    {
        if (!FillBuffer())
        {
            return std::nullopt;
        }
        newline = m_buffer.find('\n');
    }
    auto line = m_buffer.substr(0, newline);
    m_buffer.erase(0, newline + 1);
    return line;
}

bool LineSocket::FillBuffer()
{
    char chunk[4096];
    ssize_t received = 0;
    do
    {
        received = recv(m_fd, chunk, sizeof(chunk), 0);
    } while (received < 0 && errno == EINTR);

    if (received <= 0)
    {
        return false;
    }
    m_buffer.append(chunk, static_cast<std::size_t>(received));
    return true;
}

// MSG_NOSIGNAL: a client that went away gives EPIPE, not a SIGPIPE that
// would end the process.
bool LineSocket::WriteLine(std::string_view line)
{
    std::string data(line);
    data += '\n';

    std::lock_guard lock(m_writeMutex);
    std::size_t sent = 0;
    while (sent < data.size())
    {
        const auto result = send(m_fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (result < 0 && errno == EINTR)
        {
            continue;
        }
        if (result <= 0)
        {
            return false;
        }
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

void LineSocket::Shutdown() { shutdown(m_fd, SHUT_RDWR); }

fs::path ControlSocketPath()
{
    const auto folder = RuntimeFolder().value_or(File::getApplicationDataPath());
    return folder / "control.sock";
}

std::unique_ptr<LineSocket> ConnectControlSocket(const fs::path& path)
{
    const auto address = SocketAddress(path);
    if (!address)
    {
        return nullptr;
    }
    const int fd = NewSocket();
    if (fd < 0)
    {
        return nullptr;
    }
    // ENOENT: nothing runs. ECONNREFUSED: a socket file a crashed Mango left.
    if (connect(fd, reinterpret_cast<const sockaddr*>(&*address), sizeof(*address)) != 0)
    {
        close(fd);
        return nullptr;
    }
    return std::make_unique<LineSocket>(fd);
}

std::expected<int, std::string> ListenOnControlSocket(const fs::path& path)
{
    const auto address = SocketAddress(path);
    if (!address)
    {
        return std::unexpected(std::format("the path is too long for a socket: {}", path.string()));
    }
    unlink(path.c_str());
    return BindAndListen(*address, path);
}

bool IsSameUser(int fd)
{
    ucred credentials{};
    socklen_t length = sizeof(credentials);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0)
    {
        return false;
    }
    return credentials.uid == geteuid();
}

}  // namespace mango::cli
