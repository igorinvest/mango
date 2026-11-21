#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "../../media/MediaBackend.hpp"
#include "FileRecord.hpp"

// Extracts a cover/poster image for a file into its persistent metadata
// folder (see FileRecord::getMetadataFolder), kept separate from
// getStreamFolder() since that one is wiped every time a new stream is
// initiated (see StreamManager) and covers are meant to persist. Prefers a
// file's embedded artwork - either an MKV attachment named like
// cover.jpg/poster.jpg, or an MP4-style attached picture - found in the
// file's MediaInfo, and falls back to a frame 10% into the video for a file
// with no embedded artwork. The image itself is written by the
// MediaBackend. Safe to call for any file; returns std::nullopt only if
// neither is possible (e.g. an audio-only file, or the backend failing both
// ways).
class CoverExtractor
{
   public:
    // Returns the extracted cover's filename (e.g. "cover.jpg", relative to
    // fileRecord.getMetadataFolder()) on success, or std::nullopt if no
    // cover could be produced.
    [[nodiscard]] static std::optional<std::string> Extract(const FileRecord& fileRecord);

   private:
    struct Candidate
    {
        CoverSource source;
        std::string extension;
    };

    // The embedded artwork to use, if the file has any.
    [[nodiscard]] static std::optional<Candidate> FindEmbedded(const MediaInfo& info);

    // A frame early enough to usually skip intros/black frames, for a file
    // with a video track and a known duration.
    [[nodiscard]] static std::optional<Candidate> FindFrame(const MediaInfo& info);

    [[nodiscard]] static std::optional<std::string> Write(const FileRecord& fileRecord,
        const std::filesystem::path& coverFolder, const Candidate& candidate);
};
