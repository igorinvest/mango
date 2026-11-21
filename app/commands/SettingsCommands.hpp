#pragma once

#include "commands/CommandContext.hpp"
#include "commands/CommandTable.hpp"

namespace mango::cli
{

// settings, set.
void RegisterSettingsCommands(CommandTable& table, CommandContext& context);

}  // namespace mango::cli
