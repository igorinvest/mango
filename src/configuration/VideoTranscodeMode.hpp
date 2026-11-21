#pragma once

#include <optional>
#include <string>

// "automatic" preserves StreamTargetResolver's capability-based
// copy-vs-decode decision; "forceCopy"/"forceDecode" let the user override
// it (compatibility vs guaranteed-playable trade-off) regardless of what the
// source track actually is.
enum class VideoTranscodeMode
{
    Automatic,
    ForceCopy,
    ForceDecode
};

class VideoTranscodeModeSetting
{
   public:
    static VideoTranscodeMode Get();
    static void Set(VideoTranscodeMode mode);

    static std::string ToString(VideoTranscodeMode mode);
    static std::optional<VideoTranscodeMode> FromString(const std::string& value);
};
