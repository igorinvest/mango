#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../FileRecord.hpp"
#include "../../MediaInfo.hpp"
#include "ExternalFileFinder.hpp"

// Finds subtitle files that belong to a video (.srt, .vtt, .ass, .ssa):
// Movie.en.srt, Movie.English.forced.srt, and those in a Subs/Subtitles/
// "<Language> Subs" folder next to it (Subs/English.srt,
// Subs/<video name>/2_English.srt, RUS Subs/<studio>/Movie.Studio.ass).
// The search itself is ExternalFileFinder's — the rules are the same as
// for audio; this adds what's specific to
// subtitles: encoding detection and the record's externalSubtitles.
class ExternalSubtitleFinder
{
   public:
    [[nodiscard]] static const ExternalFileKind& Kind();

    // Subtitle files next to every place the same video lives, from the
    // folder scan's snapshot — see ExternalFileFinder::FindCandidates.
    [[nodiscard]] static std::vector<ExternalFileCandidate> FindCandidates(
        const DirectorySnapshot& snapshot, const std::vector<std::filesystem::path>& videoPaths);

    // Makes record.externalSubtitles match candidates: a file that's already
    // known keeps its index (and is only re-read when its size or mtime
    // changed), a new one gets the next unused index, and a file no longer
    // found is dropped. Files in folders snapshot didn't walk (the same
    // video in another added folder) are kept as they are. Returns whether
    // anything changed.
    static bool MergeInto(FileRecord& record, const std::vector<ExternalFileCandidate>& candidates,
        const DirectorySnapshot& snapshot);

    // "subrip"/"webvtt"/"ass"/"ssa" for a supported extension (any case,
    // with the dot), otherwise std::nullopt.
    [[nodiscard]] static std::optional<std::string> CodecForExtension(std::string_view extension);

   private:
    [[nodiscard]] static bool IsSubsFolder(std::string_view lowerFolderName);

    [[nodiscard]] static ExternalSubtitle Describe(
        const ExternalFileCandidate& candidate, int index);

    [[nodiscard]] static int TakeNextIndex(FileRecord& record);
};
