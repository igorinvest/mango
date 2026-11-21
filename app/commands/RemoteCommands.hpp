#pragma once

#include "commands/CommandContext.hpp"
#include "commands/CommandTable.hpp"

namespace mango::cli
{

// remote, pair, join, unpair, connect, disconnect, ping: remote streaming
// between two Mango apps (src/remote/RemoteStreaming.hpp).
void RegisterRemoteCommands(CommandTable& table, CommandContext& context);

}  // namespace mango::cli
