#include "console/Console.hpp"

#include <iostream>

#include "commands/CastCommands.hpp"
#include "commands/LanguageCommands.hpp"
#include "commands/LibraryCommands.hpp"
#include "commands/RemoteCommands.hpp"
#include "commands/SettingsCommands.hpp"
#include "commands/StreamCommands.hpp"
#include "console/ConsoleText.hpp"

namespace mango::cli
{

namespace
{

void PrintPrompt()
{
    std::cout << "mango> " << std::flush;
}

}  // namespace

Console::Console(CommandContext& context) : m_context(context)
{
    RegisterLibraryCommands(m_table, m_context);
    RegisterCastCommands(m_table, m_context);
    RegisterStreamCommands(m_table, m_context);
    RegisterSettingsCommands(m_table, m_context);
    RegisterLanguageCommands(m_table, m_context);
    RegisterRemoteCommands(m_table, m_context);
    RegisterBuiltins();
}

void Console::Run(Output& terminal)
{
    std::string line;
    PrintPrompt();
    while (!m_context.quitRequested && std::getline(std::cin, line))
    {
        Execute(line, terminal);
        if (!m_context.quitRequested)
        {
            PrintPrompt();
        }
    }
}

void Console::RegisterBuiltins()
{
    m_table.Add({"server", "", "Media server address", [this](const Args&) { PrintServer(); }});
    m_table.Add({"help", "", "This list", [this](const Args&) { PrintHelp(); }});
    m_table.Add({"quit", "", "Stop the server and exit",
        [this](const Args&) { m_context.quitRequested = true; }});
#if defined(__linux__)
    // An attached client's `quit` only detaches it (see ControlClient.hpp);
    // this stops the server for everyone.
    m_table.Add({"shutdown", "", "Stop the server, also for attached terminals",
        [this](const Args&)
        {
            m_context.out().Print("Stopping Mango.");
            m_context.quitRequested = true;
        }});
#endif
}

void Console::Execute(const std::string& line, Output& out)
{
    std::lock_guard lock(m_mutex);
    auto* const previous = m_context.output;
    m_context.output = &out;
    ExecuteLine(line);
    m_context.output = previous;
}

void Console::ExecuteLine(const std::string& line)
{
    auto words = Tokenize(line);
    if (words.empty() || words.front().starts_with('#'))
    {
        return;
    }

    const auto name = words.front();
    const auto* command = m_table.Find(name == "exit" ? "quit" : name);
    if (command == nullptr)
    {
        m_context.out().Print("Unknown command {}. Type help.", name);
        return;
    }
    words.erase(words.begin());
    command->run(words);
}

void Console::PrintHelp() const
{
    for (const auto& command : m_table.All())
    {
        m_context.out().Print("  {:<50} {}", command.name + " " + command.usage, command.help);
    }
    m_context.out().Print("\n<movie> is a number from `ls` or the start of its id. "
                 "Paths with spaces go in quotes.");
}

void Console::PrintServer() const
{
    const auto status = m_context.engine.GetStatus();
    if (!status.isListening)
    {
        m_context.out().Print("not listening (opens with the next stream){}", status.isRunning ? "" : ", engine not running");
        return;
    }
    m_context.out().Print("http://{}:{}", status.ip, status.port);
}

}  // namespace mango::cli
