#pragma once

#include <functional>
#include <string>
#include <vector>

namespace mango::cli
{

// Words typed after a command's name, already split (see Tokenize).
using Args = std::vector<std::string>;

// One console command. usage is shown after the name in `help`
// ("<movie> [audio=N]"), help is a one-line description.
struct Command
{
    std::string name;
    std::string usage;
    std::string help;
    std::function<void(const Args&)> run;
};

// Every command the console knows, in the order they were added — that's
// also the order `help` lists them in.
class CommandTable
{
   public:
    void Add(Command command);
    [[nodiscard]] const Command* Find(const std::string& name) const;
    [[nodiscard]] const std::vector<Command>& All() const;

   private:
    std::vector<Command> m_commands;
};

}  // namespace mango::cli
