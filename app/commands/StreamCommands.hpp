#pragma once

#include "commands/CommandContext.hpp"
#include "commands/CommandTable.hpp"

namespace mango::cli
{

// play, stream, progress.
void RegisterStreamCommands(CommandTable& table, CommandContext& context);

}  // namespace mango::cli
