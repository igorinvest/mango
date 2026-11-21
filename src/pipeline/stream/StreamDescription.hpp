#pragma once

#include <string>

#include "target/StreamTarget.hpp"
#include "target/StreamTargetSpecs.hpp"
#include "../library/FileRecord.hpp"
#include "../MediaInfo.hpp"

// One-line descriptions of a stream's inputs, for the log.
namespace StreamDescription
{

// "video #0 hevc, audio #1 eac3 eng, subtitles #3 subrip eng" (or
// "subtitles off").
[[nodiscard]] std::string Tracks(const TrackList& trackList);

// "DASH, 1080p, automatic"
[[nodiscard]] std::string Target(const StreamTarget& target);

// "video copy hevc, audio aac 192k" / "video h264_videotoolbox 6M max 1920x1080, ..."
[[nodiscard]] std::string Specs(const StreamTargetSpecs& specs);

// "S01E04 (3fa1…) — video #0 hevc, ... — DASH, 1080p, automatic"
[[nodiscard]] std::string Stream(const FileRecord& file, const TrackList& trackList, const StreamTarget& target);

}  // namespace StreamDescription
