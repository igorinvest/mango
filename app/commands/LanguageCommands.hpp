#pragma once

#include "commands/CommandContext.hpp"
#include "commands/CommandTable.hpp"

namespace mango::cli
{

// languages: the track preferences (see TrackPreferences) and the folder
// memory.
void RegisterLanguageCommands(CommandTable& table, CommandContext& context);

}  // namespace mango::cli
