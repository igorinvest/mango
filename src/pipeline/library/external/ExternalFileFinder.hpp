#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../../../file/DirectorySnapshot.hpp"

// What kind of file next to a video ExternalFileFinder looks for.
struct ExternalFileKind
{
    // Lowercase, with the dot: ".srt", ".mka".
    std::vector<std::string> extensions;

    // Whether a folder next to the video holds files of this kind
    // ("subs", "rus sound"). Gets the folder name lowercased.
    bool (*isKindFolder)(std::string_view lowerFolderName) = nullptr;
};

// What a file's name says about it: Movie.en.forced.srt is {"eng", forced}.
struct ExternalNameTags
{
    std::string language;
    bool forced = false;
    bool hearingImpaired = false;
    bool commentary = false;
};

// A file found next to a video.
struct ExternalFileCandidate
{
    std::filesystem::path path;

    // path relative to the video's folder, '/'-separated.
    std::string title;

    // The video's file name without extension, for ParseNameTags.
    std::string videoStem;
};

// Size and mtime of a file, so a re-scan can skip one that hasn't changed.
struct ExternalFileStamp
{
    std::uint64_t fileSizeBytes = 0;
    std::int64_t lastWriteUnixSeconds = 0;

    bool operator==(const ExternalFileStamp&) const = default;
};

// Finds the files of one kind that belong to a video, the same way for
// every kind:
//  - next to it, named after it: Movie.srt, Movie.en.srt, Movie.rus.ac3;
//  - in any folder up to kMaxFolderDepth levels below it, named after it:
//    Extras/Audio/Movie.ac3;
//  - in a video's own folder — a kind folder next to it (Subs, Rus Sound)
//    or one named after the video (Subs/Movie/), and everything below —
//    those named after the video if there are any, otherwise all of them,
//    per directory: Subs/English.srt, Rus Sound/LostFilm/1.mka.
// A folder named after another video next to it (Subs/S01E02/ for S01E01)
// is skipped with everything below. Looks only at a DirectorySnapshot of
// the folder scan's walk — nothing is listed from disk — so a directory
// the walk didn't cover has no files.
class ExternalFileFinder
{
   public:
    // kind's files next to every place the same video lives, de-duplicated
    // by path and sorted by title.
    [[nodiscard]] static std::vector<ExternalFileCandidate> FindCandidates(
        const ExternalFileKind& kind, const DirectorySnapshot& snapshot,
        const std::vector<std::filesystem::path>& videoPaths);

    // Reads language, "forced", "sdh"/"hi"/"cc" and "commentary" from the
    // part of fileName after videoStem (the whole name when it doesn't start
    // with videoStem), split on '.', '_', '-' and ' '. Numbers are ignored,
    // so "2_English.srt" is English.
    [[nodiscard]] static ExternalNameTags ParseNameTags(
        std::string_view videoStem, std::string_view fileName);

    // ParseNameTags of the candidate's file name; a language missing there
    // is taken from the folders it's in, innermost first ("RUS Subs/
    // [Studio]/Movie.Studio.ass" and "Rus Sound/LostFilm.mka" are Russian).
    [[nodiscard]] static ExternalNameTags CandidateTags(const ExternalFileCandidate& candidate);

    // Whether path has one of kind's extensions (any case).
    [[nodiscard]] static bool HasKindExtension(
        const ExternalFileKind& kind, const std::filesystem::path& path);

    // The file's size and mtime now; std::nullopt when it can't be read.
    [[nodiscard]] static std::optional<ExternalFileStamp> Stamp(const std::filesystem::path& path);

    // Whether the file at path (UTF-8) still has the given stamp.
    [[nodiscard]] static bool IsUnchanged(const std::string& path, const ExternalFileStamp& stamp);

    // Whether snapshot walked the folder holding the file at path (UTF-8)
    // — i.e. whether a file not found there is really gone.
    [[nodiscard]] static bool IsCovered(const DirectorySnapshot& snapshot, const std::string& path);

    // How many folder levels below the video are searched.
    static constexpr int kMaxFolderDepth = 3;

   private:
    // Where the video a search is for lives.
    struct VideoLocation
    {
        std::filesystem::path folder;
        std::string stem;
    };

    // kind's files for one video path.
    [[nodiscard]] static std::vector<ExternalFileCandidate> FindCandidates(
        const ExternalFileKind& kind, const DirectorySnapshot& snapshot,
        const std::filesystem::path& videoPath);

    // kind's files directly in directory.
    [[nodiscard]] static std::vector<std::filesystem::path> ListFiles(const ExternalFileKind& kind,
        const DirectorySnapshot& snapshot, const std::filesystem::path& directory);

    // Sibling files named after the video ("Movie.srt", "Movie.en.srt").
    [[nodiscard]] static std::vector<std::filesystem::path> MatchingSiblings(
        const std::vector<std::filesystem::path>& files, std::string_view videoStem);

    // directory's files named after the video. In the video's own folder
    // (isVideosFolder), all of them when none is (a single movie's
    // release, not a folder shared by a season's episodes).
    [[nodiscard]] static std::vector<std::filesystem::path> FilesInFolder(
        const ExternalFileKind& kind, const DirectorySnapshot& snapshot,
        const std::filesystem::path& directory, std::string_view videoStem,
        bool isVideosFolder);

    // FilesInFolder of directory's subfolders and theirs, down to
    // kMaxFolderDepth; depth is the subfolders' level below video.folder.
    [[nodiscard]] static std::vector<std::filesystem::path> FilesInSubfolders(
        const ExternalFileKind& kind, const DirectorySnapshot& snapshot,
        const VideoLocation& video, const std::filesystem::path& directory, int depth,
        bool isVideosFolder);

    // Whether folder, depth levels below the video, holds only this
    // video's files: a kind folder right next to it, or one named after it.
    [[nodiscard]] static bool IsVideosFolder(const ExternalFileKind& kind,
        const VideoLocation& video, const std::filesystem::path& folder, int depth);

    // Whether folder is named after another file next to the video
    // (another episode: Subs/S01E02/ when the video is S01E01).
    [[nodiscard]] static bool IsOtherVideosFolder(const DirectorySnapshot& snapshot,
        const VideoLocation& video, const std::filesystem::path& folder);

    // Whether fileStem is videoStem, or videoStem followed by a separator.
    [[nodiscard]] static bool IsNamedAfter(std::string_view fileStem, std::string_view videoStem);
};
