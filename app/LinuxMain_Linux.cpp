#include "LinuxMain.hpp"

#include <print>
#include <string_view>

#include "console/ConsoleText.hpp"
#include "console/Output.hpp"
#include "control/ControlClient.hpp"
#include "control/ControlServer.hpp"
#include "control/ControlSocket.hpp"
#include "EngineOwner.hpp"
#include "file/File.hpp"
#include "InstanceLock.hpp"

namespace mango::cli
{

namespace
{

bool HasArgument(int argc, char** argv, std::string_view wanted)
{
    for (int i = 1; i < argc; ++i)
    {
        if (argv[i] == wanted)
        {
            return true;
        }
    }
    return false;
}

ControlMessage Hello(bool service)
{
    return {.type = "hello", .version = MANGO_APP_VERSION, .owner = service ? "service" : "terminal"};
}

// A line from an attached client. Its own `quit` never gets here (it only
// detaches), so the console's quit flag after a line means `shutdown`.
LineRunner ClientLineRunner(EngineOwner& owner, bool service)
{
    return [&owner, service](const std::string& line, Output& out)
    {
        if (IsQuitCommand(Tokenize(line)))
        {
            out.Print("`quit` only detaches a terminal; `shutdown` stops Mango.");
            return false;
        }
        owner.GetConsole().Execute(line, out);
        if (!owner.QuitRequested())
        {
            return false;
        }
        if (service)
        {
            out.Print("The service stays stopped until `systemctl --user start mango` or the next boot.");
        }
        return true;
    };
}

void StartControlServer(ControlServer& server, EngineOwner& owner, bool service)
{
    if (const auto started = server.Start(ClientLineRunner(owner, service)); !started)
    {
        std::println("No control socket ({}): other terminals can't attach.", started.error());
    }
}

// The terminal that runs the engine: the prompt, as on macOS. stdin ended
// without `quit` (a piped script): keep serving until stopped.
void RunPrompt(EngineOwner& owner, Output& terminal)
{
    owner.GetConsole().Run(terminal);
    if (!owner.QuitRequested())
    {
        std::println("\nInput closed. Serving until stopped (Ctrl+C).");
        WaitUntilStopped();
    }
}

// Holding the InstanceLock. The service's events go only to the clients;
// its stdout is the journal.
int RunEngine(const std::filesystem::path& socketPath, bool service)
{
    StdoutOutput terminal;
    ControlServer server(socketPath, Hello(service), service ? nullptr : &terminal);
    EngineOwner owner(server.Events());
    owner.Start();
    StartControlServer(server, owner, service);

    if (service)
    {
        std::println("Running as the service: `mango` in a terminal attaches to it.");
        WaitUntilStopped();
    }
    else
    {
        RunPrompt(owner, terminal);
    }

    server.Stop();
    owner.Stop();
    return 0;
}

}  // namespace

int RunLinux(int argc, char** argv)
{
    const bool service = HasArgument(argc, argv, "--service");
    const auto socketPath = ControlSocketPath();
    if (!service)
    {
        if (auto client = ControlClient::Connect(socketPath))
        {
            return client->Run();
        }
    }

    const auto lock = InstanceLock::TryAcquire(File::getApplicationDataPath() / "mango.lock");
    if (!lock)
    {
        std::println("{}", lock.error());
        return 1;
    }
    return RunEngine(socketPath, service);
}

}  // namespace mango::cli
