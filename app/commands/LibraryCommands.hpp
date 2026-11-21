#pragma once

#include <optional>
#include <string>
#include <vector>

#include "commands/CommandContext.hpp"
#include "commands/CommandTable.hpp"

namespace mango::cli
{

// ls, info, folders, add-file, add-folder, rescan, scan.
void RegisterLibraryCommands(CommandTable& table, CommandContext& context);

// The library sorted by title — the numbering `ls` shows.
[[nodiscard]] std::vector<mango::LibraryEntry> SortedLibrary(const mango::Engine& engine);

// A movie by its `ls` number or the start of its fastHash. Prints why when
// there's no match.
[[nodiscard]] std::optional<mango::LibraryEntry> FindMovie(
    Output& out, const mango::Engine& engine, const std::string& reference);

}  // namespace mango::cli
