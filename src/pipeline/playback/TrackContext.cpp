#include "TrackContext.hpp"

#include "../../configuration/TrackPreferencesStore.hpp"

TrackContext TrackContext::For(const FileRecord& file, const TrackPreferences& preferences)
{
    return TrackContext{
        .preferences = preferences,
        .played = PlaybackHistory::GetInstance().Get(file.fastHash),
        .folder = FolderTrackMemory::GetInstance().Get(file.filePath),
    };
}

TrackContext TrackContext::For(const FileRecord& file) { return For(file, TrackPreferencesStore::Get()); }
