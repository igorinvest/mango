#pragma once

#include <optional>

#include "commands/CommandContext.hpp"
#include "commands/CommandTable.hpp"

namespace mango::cli
{

// devices, use, tv, pause, resume, seek, sub, stop.
void RegisterCastCommands(CommandTable& table, CommandContext& context);

// The selected TV, discovering first if none is selected yet. Prints why
// when there's none.
[[nodiscard]] std::optional<mango::SessionDevice> RequireDevice(CommandContext& context);

}  // namespace mango::cli
