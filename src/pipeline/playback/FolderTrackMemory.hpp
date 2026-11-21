#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "../../configuration/ChangeListener.hpp"
#include "../TrackRequest.hpp"
#include "../MediaInfo.hpp"

// The languages the user picked for the videos of one folder, so the rest
// of a season follows (see TrackSelector, rule 3). Languages, not track
// ids: episodes have the same languages but not always the same track
// order.
struct FolderTrackChoice
{
    std::optional<std::string> audioLanguage;  // unset: no audio pick made here
    std::optional<std::string> subtitleLanguage;
    bool subtitlesOff = false;                  // the user turned subtitles off here
    std::int64_t updatedAtUnixSeconds = 0;

    bool operator==(const FolderTrackChoice&) const = default;
};

// Per-folder track memory, in folder_tracks.json in the data directory.
// Written only for tracks the user chose (see StreamPipeline::
// RememberFolderChoice), for that kind only. A folder is a video's parent
// directory: a choice applies to the videos directly in it, not to
// subfolders. Holds at most maxFolders folders, the oldest dropped first.
// Safe from any thread.
class FolderTrackMemory
{
   public:
    static constexpr std::size_t maxFolders = 500;

    // The one in the data directory.
    static FolderTrackMemory& GetInstance();

    // Kept in file; an empty path keeps it in memory only (tests). A file
    // that can't be read is logged and starts over empty.
    explicit FolderTrackMemory(std::filesystem::path file);

    FolderTrackMemory(const FolderTrackMemory&) = delete;
    FolderTrackMemory& operator=(const FolderTrackMemory&) = delete;

    // The choice for videoFile's folder.
    [[nodiscard]] std::optional<FolderTrackChoice> Get(const std::filesystem::path& videoFile) const;

    // The user picked audio in language for videoFile.
    void RememberAudio(const std::filesystem::path& videoFile, const std::string& language);

    // The user picked subtitles in language for videoFile, or turned them
    // off (std::nullopt).
    void RememberSubtitle(const std::filesystem::path& videoFile, const std::optional<std::string>& language);

    // A stream of videoFile starts with trackList: remembers the languages
    // of the kinds request chose (subtitles -1: off), and only those. A
    // track with no known language writes nothing: it can't be matched in
    // the next video.
    void RememberChosen(const std::filesystem::path& videoFile, const TrackRequest& request, const TrackList& trackList);

    // Forgets every folder ("Forget folder tracks").
    void Clear();

    [[nodiscard]] std::size_t Size() const;

    // Called after every change (it shows in the library's entries),
    // possibly with this class's lock held.
    void SetChangeListener(ChangeListener::Handler handler);

   private:
    [[nodiscard]] static std::string FolderKey(const std::filesystem::path& videoFile);

    // The choice for folderKey, stamped now; creates it, and drops the
    // oldest folder when there are too many. Callers hold m_mutex.
    FolderTrackChoice& TouchLocked(const std::string& folderKey);
    void TrimLocked(const std::string& keep);

    void Load();
    void SaveLocked() const;
    void Changed();

    const std::filesystem::path m_file;
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, FolderTrackChoice> m_folders;
    ChangeListener m_changes;
};
