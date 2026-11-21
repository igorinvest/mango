#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../../../media/VideoEncoderOption.hpp"
#include "StreamProfile.hpp"

// What the native apps' Supported Formats window shows and edits: a
// simplified view of one StreamProfile's capabilities - each listed video
// codec's highest supported resolution, each listed audio codec on/off, and
// rotationSupported. Finer limits (fps, channels, sample rate) stay in
// stream-profiles.json only.
struct SupportedVideoFormat
{
    std::string codec;
    // "720p", "1080p", "4k", "custom" (a size matching no preset), or
    // std::nullopt when the codec is not supported at all.
    std::optional<std::string> maxResolution;
};

struct SupportedAudioFormat
{
    std::string codec;
    bool supported{false};
};

struct SupportedFormatsInfo
{
    std::vector<SupportedVideoFormat> video;
    std::vector<SupportedAudioFormat> audio;
    bool rotationSupported{false};
    // What this machine converts video to for the protocol, best first
    // (MediaBackend::ConversionOptions); empty when it can't convert.
    std::vector<VideoEncoderOption> conversion;
};

class SupportedFormats
{
   public:
    // ffprobe codec names, in the order the window lists them.
    static const std::vector<std::string> kVideoCodecs;
    static const std::vector<std::string> kAudioCodecs;

    [[nodiscard]] static SupportedFormatsInfo Describe(const StreamProfile& profile);

    [[nodiscard]] static bool IsListedVideoCodec(const std::string& codec);
    [[nodiscard]] static bool IsListedAudioCodec(const std::string& codec);

   private:
    [[nodiscard]] static std::optional<std::string> MaxResolution(
        const StreamProfile& profile, const std::string& codec);

    [[nodiscard]] static std::string ResolutionName(const VideoCodecConfig& entry);
};
