#include "SupportedFormats.hpp"

#include <algorithm>

#include "../target/StreamTargetResolver.hpp"

const std::vector<std::string> SupportedFormats::kVideoCodecs = {"h264", "hevc", "vp9", "av1"};
const std::vector<std::string> SupportedFormats::kAudioCodecs = {"aac", "opus", "ac3", "eac3"};

SupportedFormatsInfo SupportedFormats::Describe(const StreamProfile& profile)
{
    SupportedFormatsInfo info;
    info.rotationSupported = profile.rotationSupported;

    for (const auto& codec : kVideoCodecs)
    {
        info.video.push_back({.codec = codec, .maxResolution = MaxResolution(profile, codec)});
    }

    for (const auto& codec : kAudioCodecs)
    {
        const bool supported = std::ranges::contains(
            profile.audioCapabilities, codec, &AudioCodecConfig::codec);
        info.audio.push_back({.codec = codec, .supported = supported});
    }

    return info;
}

bool SupportedFormats::IsListedVideoCodec(const std::string& codec)
{
    return std::ranges::contains(kVideoCodecs, codec);
}

bool SupportedFormats::IsListedAudioCodec(const std::string& codec)
{
    return std::ranges::contains(kAudioCodecs, codec);
}

std::optional<std::string> SupportedFormats::MaxResolution(
    const StreamProfile& profile, const std::string& codec)
{
    const VideoCodecConfig* largest = nullptr;
    for (const auto& entry : profile.videoCapabilities)
    {
        if (entry.codec == codec &&
            (!largest || entry.maxWidth * entry.maxHeight > largest->maxWidth * largest->maxHeight))
        {
            largest = &entry;
        }
    }

    if (!largest)
    {
        return std::nullopt;
    }
    return ResolutionName(*largest);
}

std::string SupportedFormats::ResolutionName(const VideoCodecConfig& entry)
{
    const auto& presets = StreamProfiles::BuiltInEncodingDefaults();

    for (const auto presetId : EncodingPresetSetting::kAll)
    {
        const auto& preset = presets.Preset(presetId);

        // Exact match either way round - a box and its upright twin are the
        // same size to the long-side rule.
        const bool sameSize =
            StreamTargetResolver::FitsWithin(
                entry.maxWidth, entry.maxHeight, preset.maxWidth, preset.maxHeight) &&
            StreamTargetResolver::FitsWithin(
                preset.maxWidth, preset.maxHeight, entry.maxWidth, entry.maxHeight);

        if (sameSize)
        {
            return EncodingPresetSetting::ToString(presetId);
        }
    }
    return "custom";
}
