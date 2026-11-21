#include "commands/CommandTable.hpp"

#include <algorithm>

namespace mango::cli
{

void CommandTable::Add(Command command) { m_commands.push_back(std::move(command)); }

const Command* CommandTable::Find(const std::string& name) const
{
    const auto it = std::ranges::find(m_commands, name, &Command::name);
    return it == m_commands.end() ? nullptr : &*it;
}

const std::vector<Command>& CommandTable::All() const { return m_commands; }

}  // namespace mango::cli
