#pragma once

#include <optional>

#include "../library/FileRecord.hpp"
#include "FolderTrackMemory.hpp"
#include "PlaybackHistory.hpp"
#include "TrackPreferences.hpp"

// Everything TrackSelector decides from besides the file and the request:
// the user's preferences and what's remembered about the file and its
// folder. Built by the engine's callers; TrackSelector reads nothing else.
struct TrackContext
{
    TrackPreferences preferences;
    std::optional<PlaybackRecord> played;
    std::optional<FolderTrackChoice> folder;

    // The engine's current state for file: its history and its folder's
    // memory, with preferences already read.
    [[nodiscard]] static TrackContext For(const FileRecord& file, const TrackPreferences& preferences);

    // The same, reading the preferences too.
    [[nodiscard]] static TrackContext For(const FileRecord& file);
};
