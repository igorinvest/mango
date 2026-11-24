#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "SessionState.hpp"

namespace mango
{

// The library grid's sections and their order, as both apps drew them
// (see docs/implemented/add-folder-scope.md): "Added Manually" first, then
// each added folder in tree order — a folder's own videos, then its
// subfolders, each followed by its own — then one section per paired
// device whose library this one shows (see LibraryEntry::remotePeerId),
// each followed by a flat "folder" section per folder name its videos are
// in (LibraryEntry::folderNames). Sorting only reorders siblings, so a
// subfolder never leaves its parent. Search and collapsing stay in the apps.
namespace LibrarySections
{

inline constexpr std::string_view manualId = "manual";

// A paired device's library: "remote:<its deviceId>", kind "remote". Its
// folders: "remote:<its deviceId>/<folder name>", kind "folder".
inline constexpr std::string_view remotePrefix = "remote:";

enum class SortOrder
{
    Name,
    DateAdded
};

[[nodiscard]] std::optional<SortOrder> SortOrderFromString(std::string_view text);
[[nodiscard]] std::string ToString(SortOrder order);

// Finder-style name order (localizedStandardCompare): case-insensitive, and
// digit runs compare by value, so "Episode 2" comes before "Episode 10".
// Negative, 0 or positive.
[[nodiscard]] int CompareNames(std::string_view a, std::string_view b);

// Name A→Z; date added newest first. Ties fall back to name, then fastHash,
// so the order is total and cards don't swap places between refreshes.
void SortEntries(std::vector<const LibraryEntry*>& entries, SortOrder order);

[[nodiscard]] std::vector<LibrarySection> Build(
    const std::vector<LibraryEntry>& entries, const std::vector<LibraryFolderEntry>& folders, SortOrder order);

// The video after fastHash in sectionId, in sections' order — nullopt at the
// section's end, or if either isn't there (any more).
[[nodiscard]] std::optional<std::string> NextFastHash(
    const std::vector<LibrarySection>& sections, const std::string& sectionId, const std::string& fastHash);

}  // namespace LibrarySections

}  // namespace mango
