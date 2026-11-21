#pragma once

#include <mutex>
#include <string>

#include "commands/CommandContext.hpp"
#include "commands/CommandTable.hpp"
#include "console/Output.hpp"

namespace mango::cli
{

// Reads commands from stdin, one per line, until `quit` or the end of
// input. Works typed by hand or piped from a script. On Linux, lines from
// attached clients run here too (see ControlServer.hpp).
class Console
{
   public:
    explicit Console(CommandContext& context);

    // The prompt on stdin; each line prints to terminal.
    void Run(Output& terminal);

    // Runs one line, printing to out. One command at a time: a line from
    // another thread waits until the running one is done.
    void Execute(const std::string& line, Output& out);

   private:
    void RegisterBuiltins();
    void ExecuteLine(const std::string& line);
    void PrintHelp() const;
    void PrintServer() const;

    CommandContext& m_context;
    CommandTable m_table;
    std::mutex m_mutex;
};

}  // namespace mango::cli
