#include "control/ControlClient.hpp"

#include <cstdlib>
#include <iostream>
#include <thread>

#include "console/ConsoleText.hpp"

namespace mango::cli
{

namespace
{

void PrintPrompt() { std::cout << "mango> " << std::flush; }

// Double quotes, with a backslash before " and \ (see Tokenize).
std::string Quote(const std::string& text)
{
    std::string quoted = "\"";
    for (const char c : text)
    {
        if (c == '"' || c == '\\')
        {
            quoted += '\\';
        }
        quoted += c;
    }
    return quoted + "\"";
}

std::string JoinWords(const std::vector<std::string>& words, std::size_t from)
{
    std::string joined;
    for (std::size_t i = from; i < words.size(); ++i)
    {
        joined += (joined.empty() ? "" : " ") + words[i];
    }
    return joined;
}

void PrintHello(Output& out, const ControlMessage& hello)
{
    const auto owner = hello.owner.value_or("");
    out.Print("Connected to {}. `quit` leaves it running; `shutdown` stops it.",
        owner == "service" ? "the Mango service" : "the Mango running in another terminal");

    const auto version = hello.version.value_or("?");
    if (version != MANGO_APP_VERSION)
    {
        out.Print("It's Mango {}, this is {}: some commands may differ.", version, MANGO_APP_VERSION);
    }
}

}  // namespace

std::unique_ptr<ControlClient> ControlClient::Connect(const std::filesystem::path& socketPath)
{
    auto socket = ConnectControlSocket(socketPath);
    return socket ? std::make_unique<ControlClient>(std::move(socket)) : nullptr;
}

ControlClient::ControlClient(std::unique_ptr<LineSocket> socket) : m_socket(std::move(socket)) {}

int ControlClient::Run()
{
    if (!ReadHello())
    {
        m_out.Print("Mango didn't answer on its control socket.");
        return 1;
    }
    std::thread reader([this] { ReadReplies(); });

    std::string line;
    PrintPrompt();
    while (std::getline(std::cin, line))
    {
        const auto words = Tokenize(line);
        if (IsQuitCommand(words))
        {
            m_out.Print("Detached. Mango keeps running.");
            break;
        }
        if (!words.empty() && !words.front().starts_with('#'))
        {
            SendAndWait(PrepareLineForServer(line));
        }
        PrintPrompt();
    }

    m_detaching = true;
    m_socket->Shutdown();
    reader.join();
    return 0;
}

bool ControlClient::ReadHello()
{
    const auto line = m_socket->ReadLine();
    const auto hello = line ? ReadControlMessage(*line) : std::nullopt;
    if (!hello || hello->type != "hello")
    {
        return false;
    }
    PrintHello(m_out, *hello);
    return true;
}

// The reader thread: output and events as they arrive.
void ControlClient::ReadReplies()
{
    while (const auto line = m_socket->ReadLine())
    {
        if (const auto message = ReadControlMessage(*line))
        {
            Handle(*message);
        }
    }
    if (!m_detaching)
    {
        ServerGone();
    }
}

void ControlClient::Handle(const ControlMessage& message)
{
    if ((message.type == "output" || message.type == "event") && message.text)
    {
        m_out.Write(*message.text);
        return;
    }
    if (message.type == "done")
    {
        {
            std::lock_guard lock(m_mutex);
            m_doneId = message.id.value_or(0);
        }
        m_done.notify_all();
    }
}

void ControlClient::SendAndWait(const std::string& line)
{
    std::unique_lock lock(m_mutex);
    const auto id = ++m_lastId;
    if (!m_socket->WriteLine(WriteControlMessage({.type = "command", .id = id, .line = line})))
    {
        lock.unlock();
        ServerGone();
    }
    m_done.wait(lock, [&] { return m_doneId >= id; });
}

// The service restarted, or the terminal that ran Mango quit. The main
// thread may be blocked reading stdin, so the process ends here.
void ControlClient::ServerGone()
{
    m_out.Print("\nMango stopped.");
    std::_Exit(0);
}

bool IsQuitCommand(const std::vector<std::string>& words)
{
    return !words.empty() && (words.front() == "quit" || words.front() == "exit");
}

std::string PrepareLineForServer(const std::string& line)
{
    const auto words = Tokenize(line);
    if (words.size() < 2 || (words.front() != "add-file" && words.front() != "add-folder"))
    {
        return line;
    }
    return words.front() + " " + Quote(ResolveUserPath(JoinWords(words, 1)));
}

}  // namespace mango::cli
