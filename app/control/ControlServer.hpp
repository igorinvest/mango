#pragma once

#include <atomic>
#include <expected>
#include <filesystem>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "console/Output.hpp"
#include "control/ControlMessages.hpp"
#include "control/ControlSocket.hpp"

namespace mango::cli
{

// Runs one line a client typed, printing to out. Returns true when the line
// asked to stop the server (`shutdown`).
using LineRunner = std::function<bool(const std::string& line, Output& out)>;

// Linux: the control socket of the Mango that runs the engine (the service,
// or the first terminal). Later `mango`s connect to it (see
// ControlClient.hpp): each line they send runs here, and what it prints
// goes back to them. Notices go to every attached client. Each client has
// its own thread; their commands run one at a time (Console::Execute).
class ControlServer
{
   public:
    // hello: what every client gets first. terminal: this process's own
    // terminal, which gets the events too, or nullptr (the service).
    ControlServer(std::filesystem::path socketPath, ControlMessage hello, Output* terminal);
    ~ControlServer();

    ControlServer(const ControlServer&) = delete;
    ControlServer& operator=(const ControlServer&) = delete;

    // For the watchers: the terminal, if any, and every attached client.
    [[nodiscard]] Output& Events() { return m_events; }

    // Opens the socket and accepts clients from now on.
    std::expected<void, std::string> Start(LineRunner run);

    // Closes the socket and every connection (the clients say Mango
    // stopped), and waits for their threads, a running command included.
    void Stop();

   private:
    struct Connection
    {
        std::unique_ptr<LineSocket> socket;
        std::thread thread;
        std::atomic<bool> finished = false;
    };

    // Events() sends each text to every client as an event.
    class EventOutput final : public Output
    {
       public:
        explicit EventOutput(ControlServer& server) : m_server(server) {}
        void Write(std::string_view text) override { m_server.Broadcast(text); }

       private:
        ControlServer& m_server;
    };

    void AcceptLoop();
    [[nodiscard]] bool WaitForClient() const;
    void Attach(int fd);
    void Serve(Connection& connection);
    void RunCommand(LineSocket& socket, const ControlMessage& command);
    void Broadcast(std::string_view text);
    void ReapFinished();

    std::filesystem::path m_socketPath;
    ControlMessage m_hello;
    Output* m_terminal;
    EventOutput m_events{*this};
    LineRunner m_run;

    int m_listenFd = -1;
    // Stop writes a byte to wake the accept loop.
    int m_wakeRead = -1;
    int m_wakeWrite = -1;
    std::atomic<bool> m_stopping = false;
    std::thread m_acceptThread;
    std::mutex m_mutex;
    std::list<std::unique_ptr<Connection>> m_connections;
};

}  // namespace mango::cli
