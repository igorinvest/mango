#include "control/ControlServer.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>

namespace mango::cli
{

namespace
{

// What one client's command prints, sent to it as output messages.
class ClientOutput final : public Output
{
   public:
    ClientOutput(LineSocket& socket, std::uint64_t id) : m_socket(socket), m_id(id) {}

    void Write(std::string_view text) override
    {
        m_socket.WriteLine(WriteControlMessage({.type = "output", .id = m_id, .text = std::string(text)}));
    }

   private:
    LineSocket& m_socket;
    std::uint64_t m_id;
};

}  // namespace

ControlServer::ControlServer(std::filesystem::path socketPath, ControlMessage hello, Output* terminal)
    : m_socketPath(std::move(socketPath)), m_hello(std::move(hello)), m_terminal(terminal)
{
}

ControlServer::~ControlServer() { Stop(); }

std::expected<void, std::string> ControlServer::Start(LineRunner run)
{
    int wake[2] = {-1, -1};
    if (pipe2(wake, O_CLOEXEC) != 0)
    {
        return std::unexpected(std::string("pipe2 failed"));
    }
    const auto fd = ListenOnControlSocket(m_socketPath);
    if (!fd)
    {
        close(wake[0]);
        close(wake[1]);
        return std::unexpected(fd.error());
    }
    m_listenFd = *fd;
    m_wakeRead = wake[0];
    m_wakeWrite = wake[1];
    m_run = std::move(run);
    m_acceptThread = std::thread([this] { AcceptLoop(); });
    return {};
}

void ControlServer::Stop()
{
    if (m_listenFd < 0 || m_stopping.exchange(true))
    {
        return;
    }

    const char byte = 1;
    [[maybe_unused]] const auto written = write(m_wakeWrite, &byte, 1);
    m_acceptThread.join();
    close(m_listenFd);
    close(m_wakeRead);
    close(m_wakeWrite);
    unlink(m_socketPath.c_str());

    std::list<std::unique_ptr<Connection>> connections;
    {
        std::lock_guard lock(m_mutex);
        connections.swap(m_connections);
    }
    for (auto& connection : connections)
    {
        connection->socket->Shutdown();
    }
    // Outside the lock: a command still running may print an event.
    for (auto& connection : connections)
    {
        connection->thread.join();
    }
}

void ControlServer::AcceptLoop()
{
    while (WaitForClient())
    {
        const int fd = accept4(m_listenFd, nullptr, nullptr, SOCK_CLOEXEC);
        if (fd >= 0)
        {
            Attach(fd);
        }
        else if (errno != EINTR && errno != ECONNABORTED)
        {
            return;
        }
    }
}

// false once Stop wakes it.
bool ControlServer::WaitForClient() const
{
    pollfd fds[2] = {{.fd = m_listenFd, .events = POLLIN, .revents = 0}, {.fd = m_wakeRead, .events = POLLIN, .revents = 0}};
    while (poll(fds, 2, -1) < 0)
    {
        if (errno != EINTR)
        {
            return false;
        }
    }
    return fds[1].revents == 0 && (fds[0].revents & POLLIN) != 0;
}

// Another user's process is turned away, on top of the socket's 0600.
void ControlServer::Attach(int fd)
{
    if (!IsSameUser(fd))
    {
        close(fd);
        return;
    }

    auto connection = std::make_unique<Connection>();
    connection->socket = std::make_unique<LineSocket>(fd);

    // Under the lock Broadcast takes: hello comes first, and every event
    // after it reaches this client.
    std::lock_guard lock(m_mutex);
    connection->socket->WriteLine(WriteControlMessage(m_hello));
    ReapFinished();
    auto& added = *m_connections.emplace_back(std::move(connection));
    added.thread = std::thread([this, &added] { Serve(added); });
}

void ControlServer::Serve(Connection& connection)
{
    while (const auto line = connection.socket->ReadLine())
    {
        const auto message = ReadControlMessage(*line);
        if (message && message->type == "command" && message->line)
        {
            RunCommand(*connection.socket, *message);
        }
    }
    connection.finished = true;
}

// After `shutdown`, the reply is sent first; then the process stops the way
// `systemctl stop` stops it: the engine's SIGTERM handler stops ffmpeg,
// saves playback positions and exits with 0, so systemd doesn't restart it.
void ControlServer::RunCommand(LineSocket& socket, const ControlMessage& command)
{
    const auto id = command.id.value_or(0);
    ClientOutput out(socket, id);
    const bool stop = m_run(*command.line, out);
    socket.WriteLine(WriteControlMessage({.type = "done", .id = id}));
    if (stop)
    {
        std::raise(SIGTERM);
    }
}

void ControlServer::Broadcast(std::string_view text)
{
    if (m_terminal != nullptr)
    {
        m_terminal->Write(text);
    }

    const auto event = WriteControlMessage({.type = "event", .text = std::string(text)});
    std::lock_guard lock(m_mutex);
    for (const auto& connection : m_connections)
    {
        if (!connection->finished)
        {
            connection->socket->WriteLine(event);
        }
    }
}

// Under m_mutex. A finished thread has returned, so the join is immediate.
void ControlServer::ReapFinished()
{
    std::erase_if(m_connections,
        [](const std::unique_ptr<Connection>& connection)
        {
            if (!connection->finished)
            {
                return false;
            }
            connection->thread.join();
            return true;
        });
}

}  // namespace mango::cli
