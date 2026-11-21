// Linux: the control socket the service and attached terminals share, and
// the instance lock (app/control/ControlServer.hpp, app/InstanceLock.hpp). No engine:
// the server runs lines through a fake.

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <mutex>
#include <string>
#include <vector>

#include "console/ConsoleText.hpp"
#include "console/Output.hpp"
#include "control/ControlClient.hpp"
#include "control/ControlMessages.hpp"
#include "control/ControlServer.hpp"
#include "control/ControlSocket.hpp"
#include "InstanceLock.hpp"
#include "TestCheck.hpp"

namespace fs = std::filesystem;
using namespace mango::cli;

namespace
{

// Short and per process: socket paths are limited to about 108 bytes.
fs::path TempPath(const std::string& name)
{
    return fs::temp_directory_path() / std::format("mango-test-{}-{}", getpid(), name);
}

// The terminal of a server under test.
class RecordingOutput final : public Output
{
   public:
    void Write(std::string_view text) override
    {
        std::lock_guard lock(m_mutex);
        m_text += text;
    }

    std::string Text()
    {
        std::lock_guard lock(m_mutex);
        return m_text;
    }

   private:
    std::mutex m_mutex;
    std::string m_text;
};

std::optional<ControlMessage> NextMessage(LineSocket& socket)
{
    const auto line = socket.ReadLine();
    return line ? ReadControlMessage(*line) : std::nullopt;
}

// Sends line as command id and returns what came back until its done.
std::string RunRemotely(LineSocket& socket, std::uint64_t id, const std::string& line)
{
    socket.WriteLine(WriteControlMessage({.type = "command", .id = id, .line = line}));
    std::string printed;
    while (const auto message = NextMessage(socket))
    {
        if (message->type == "done")
        {
            Check(message->id == id, "done has the command's id");
            break;
        }
        Check(message->type == "output" && message->id == id, "output has the command's id");
        printed += message->text.value_or("");
    }
    return printed;
}

void LockIsExclusive()
{
    const auto file = TempPath("lock");
    {
        auto first = InstanceLock::TryAcquire(file);
        Check(first.has_value(), "the first lock is taken");
        const auto second = InstanceLock::TryAcquire(file);
        Check(!second.has_value(), "a second lock on the same file fails");
        Check(!second && second.error().contains("already running"), "the error says Mango is running");
    }
    Check(InstanceLock::TryAcquire(file).has_value(), "the lock is free again once its holder is gone");
    fs::remove(file);
}

void MessageRoundTrip()
{
    const ControlMessage sent{.type = "output", .id = 7, .text = "Casting.\nDone \"x\"\n"};
    const auto json = WriteControlMessage(sent);
    Check(!json.contains('\n'), "a message is one line");

    const auto read = ReadControlMessage(json);
    Check(read && read->type == "output" && read->id == 7 && read->text == sent.text, "a message reads back");
    Check(read && !read->line && !read->owner, "unset fields stay unset");
    Check(!ReadControlMessage("not json"), "a line that isn't JSON is no message");
}

void PathsBecomeAbsolute()
{
    const std::string home = std::getenv("HOME") ? std::getenv("HOME") : "";
    const auto words = Tokenize(PrepareLineForServer("add-folder ~/My \"Movies\""));
    Check(words.size() == 2 && words[0] == "add-folder" && words[1] == home + "/My Movies",
        "add-folder: ~ expanded, the words joined, one quoted argument");

    const auto relative = Tokenize(PrepareLineForServer("add-file clip.mkv"));
    Check(relative.size() == 2 && relative[1] == (fs::current_path() / "clip.mkv").string(),
        "add-file: a relative path becomes absolute");

    Check(PrepareLineForServer("ls ~/x") == "ls ~/x", "other commands go as typed");
    Check(IsQuitCommand(Tokenize("exit")) && !IsQuitCommand(Tokenize("shutdown")), "quit and exit detach");
}

void TwoClientsGetTheirOwnOutput()
{
    const auto path = TempPath("sock");
    RecordingOutput terminal;
    ControlServer server(path, {.type = "hello", .version = "9.9", .owner = "service"}, &terminal);
    const auto started = server.Start(
        [](const std::string& line, Output& out)
        {
            out.Print("ran {}", line);
            return false;
        });
    Check(started.has_value(), "the server listens");

    auto first = ConnectControlSocket(path);
    auto second = ConnectControlSocket(path);
    Check(first && second, "two clients connect");
    if (!first || !second)
    {
        return;
    }

    const auto hello = NextMessage(*first);
    Check(hello && hello->type == "hello" && hello->owner == "service" && hello->version == "9.9", "hello first");
    Check(NextMessage(*second).has_value(), "the second client gets hello too");

    Check(RunRemotely(*first, 1, "ls") == "ran ls\n", "the first client gets its command's output");
    Check(RunRemotely(*second, 1, "settings") == "ran settings\n", "the second client gets its own");

    server.Events().Write("\nPlaying next\n");
    for (auto* client : {first.get(), second.get()})
    {
        const auto event = NextMessage(*client);
        Check(event && event->type == "event" && event->text == "\nPlaying next\n", "an event reaches every client");
    }
    Check(terminal.Text() == "\nPlaying next\n", "and the server's own terminal");

    server.Stop();
    Check(!first->ReadLine(), "a client sees the server go away");
    Check(!fs::exists(path) && !ConnectControlSocket(path), "the socket is gone after Stop");
}

// A Mango killed with SIGKILL leaves its socket file behind.
void StaleSocketIsReplaced()
{
    const auto path = TempPath("stale");
    const auto stale = ListenOnControlSocket(path);
    Check(stale.has_value(), "a socket is made");
    if (stale)
    {
        close(*stale);
    }
    Check(fs::exists(path) && !ConnectControlSocket(path), "nothing answers on a stale socket");

    ControlServer server(path, {.type = "hello"}, nullptr);
    Check(server.Start([](const std::string&, Output&) { return false; }).has_value(), "a new server replaces it");
    Check(ConnectControlSocket(path) != nullptr, "and clients reach it");
    server.Stop();
}

}  // namespace

std::vector<TestCase> ControlTestCases()
{
    return {
        {"instance lock is exclusive", LockIsExclusive},
        {"control message round trip", MessageRoundTrip},
        {"control client paths", PathsBecomeAbsolute},
        {"control server with two clients", TwoClientsGetTheirOwnOutput},
        {"control socket stale file", StaleSocketIsReplaced},
    };
}
