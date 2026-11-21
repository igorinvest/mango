#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../FileRecord.hpp"
#include "../../MediaInfo.hpp"
#include "ExternalFileFinder.hpp"

// Finds audio files that belong to a video (.mka, .ac3, .eac3, .dts, .aac,
// .m4a, .flac, .mp3, .opus): Movie.rus.ac3, Movie.en.commentary.mka, and
// those in an Audio/Sound/Sounds/"<Language> Sound" folder next to it
// (Rus Sound/<studio>/Movie.mka). The search is ExternalFileFinder's —
// the rules are the same as for subtitles; this adds probing each file (codec,
// channels, timing) and the record's externalAudios.
class ExternalAudioFinder
{
   public:
    [[nodiscard]] static const ExternalFileKind& Kind();

    // Audio files next to every place the same video lives, from the
    // folder scan's snapshot — see ExternalFileFinder::FindCandidates.
    [[nodiscard]] static std::vector<ExternalFileCandidate> FindCandidates(
        const DirectorySnapshot& snapshot, const std::vector<std::filesystem::path>& videoPaths);

    // Makes record.externalAudios match candidates: a file that's already
    // known keeps its index (and is only probed again when its size or mtime
    // changed), a new one is probed and gets the next unused index, and a
    // file no longer found is dropped. Files in folders snapshot didn't walk
    // (the same video in another added folder) are kept as they are. A file
    // that can't be probed isn't added, and isn't probed again until its
    // size or mtime changes. Returns whether anything changed.
    static bool MergeInto(FileRecord& record, const std::vector<ExternalFileCandidate>& candidates,
        const DirectorySnapshot& snapshot);

   private:
    [[nodiscard]] static bool IsAudioFolder(std::string_view lowerFolderName);

    // Probes the file; std::nullopt when it can't be read as audio.
    [[nodiscard]] static std::optional<ExternalAudio> Describe(
        const ExternalFileCandidate& candidate, int index);

    // The merged entry for one candidate, or std::nullopt when it can't be
    // probed.
    [[nodiscard]] static std::optional<ExternalAudio> MergeCandidate(
        FileRecord& record, const ExternalFileCandidate& candidate);

    [[nodiscard]] static int NextIndex(const FileRecord& record);

    // Files that couldn't be probed, by path, with what they looked like
    // then. In memory only: each one is tried once more after a restart.
    [[nodiscard]] static bool IsKnownUnreadable(const std::filesystem::path& path);
    static void RememberUnreadable(const std::filesystem::path& path);

    static std::mutex s_unreadableMutex;
    static std::unordered_map<std::string, ExternalFileStamp> s_unreadable;
};
