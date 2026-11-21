#include "StreamTargetResolver.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

#include "../../../media/MediaBackend.hpp"

StreamTargetSpecs StreamTargetResolver::Resolve(const std::optional<VideoTrack> videoTrack,
    const std::optional<AudioTrack> audioTrack, const std::optional<SubtitleTrack> subtitleTrack,
    const StreamTarget& target)
{
    const auto& profile = target.profile;
    const auto defaults = target.encoding ? *target.encoding : StreamProfiles::GetEncodingDefaults();
    const auto& selected = defaults.Preset(target.preset);

    StreamTargetSpecs specs;

    // Without video, audio is encoded at the selected preset's bitrate.
    const EncodingPreset* ladderPreset = &selected;

    if(videoTrack.has_value())
    {
        specs.video = ResolveVideo(*videoTrack, target, defaults, selected);
        ladderPreset = &LadderPreset(defaults, OutputSize(*videoTrack, specs.video));
    }

    if (audioTrack.has_value())
    {
        specs.audio = ResolveAudio(*audioTrack, profile, defaults, *ladderPreset);
    }

    if (subtitleTrack.has_value())
    {
        specs.subtitle = ResolveSubtitle(*subtitleTrack, profile);
    }

    return specs;
}

bool StreamTargetResolver::FitsWithin(int width, int height, int maxWidth, int maxHeight)
{
    return std::max(width, height) <= std::max(maxWidth, maxHeight) &&
           std::min(width, height) <= std::min(maxWidth, maxHeight);
}

VideoTargetSpec StreamTargetResolver::ResolveVideo(const VideoTrack& track,
    const StreamTarget& target, const EncodingDefaults& defaults, const EncodingPreset& selected)
{
    if (ShouldCopyVideo(track, target, selected))
    {
        return {.copy = true, .codec = "copy", .preset = "", .bitrate = "", .pix_fmt = ""};
    }

    // Empty when nothing can convert for this profile; the backend then
    // refuses the conversion with ConversionError::NoVideoEncoder.
    const auto encoder = MediaBackends::Get().VideoEncoder(target.profile);

    VideoTargetSpec spec{.copy = false,
        .codec = encoder ? encoder->encoder : "",
        .preset = "",
        .bitrate = "",
        .pix_fmt = defaults.videoPixFmt,
        .maxWidth = selected.maxWidth,
        .maxHeight = selected.maxHeight,
        .deinterlace = track.interlaced.value_or(false),
        .driver = encoder ? encoder->driver : ""};

    spec.bitrate = LadderPreset(defaults, OutputSize(track, spec)).videoBitrate;
    return spec;
}

AudioTargetSpec StreamTargetResolver::ResolveAudio(const AudioTrack& track,
    const StreamProfile& profile, const EncodingDefaults& defaults,
    const EncodingPreset& ladderPreset)
{
    if (CanCopyAudio(track, profile))
    {
        return {.copy = true, .codec = "copy", .bitrate = ""};
    }

    return {.copy = false, .codec = defaults.audioCodec, .bitrate = ladderPreset.audioBitrate};
}

SubtitleTargetSpec StreamTargetResolver::ResolveSubtitle(
    const SubtitleTrack& /*track*/, const StreamProfile& profile)
{
    if (profile.subtitleCapabilities.empty())
    {
        return {};
    }
    return {.codec = profile.subtitleCapabilities.front().codec, .bitrate = ""};
}

bool StreamTargetResolver::ShouldCopyVideo(
    const VideoTrack& track, const StreamTarget& target, const EncodingPreset& selected)
{
    switch (target.mode)
    {
        case VideoTranscodeMode::ForceCopy:
            return true;
        case VideoTranscodeMode::ForceDecode:
            return false;
        case VideoTranscodeMode::Automatic:
        default:
            return CanCopyVideo(track, target.profile, selected);
    }
}

bool StreamTargetResolver::CanCopyVideo(
    const VideoTrack& track, const StreamProfile& profile, const EncodingPreset& selected)
{
    // The preset caps copying too: a 4K file the TV could play is still
    // converted on the 1080p preset.
    if (!FitsWithin(track.width, track.height, selected.maxWidth, selected.maxHeight))
    {
        return false;
    }

    // Copied frames keep their display-matrix flag; a TV that ignores it
    // plays the video sideways, while converting rotates the frames.
    if (track.rotation.value_or(0) != 0 && !profile.rotationSupported)
    {
        return false;
    }

    // Cast devices can't decode field-coded H.264 (1080i HDTV recordings):
    // a copied stream shows a black screen or fails to load.
    if (track.interlaced.value_or(false))
    {
        return false;
    }

    return MatchesCapability(track, profile);
}

bool StreamTargetResolver::MatchesCapability(const VideoTrack& track, const StreamProfile& profile)
{
    return std::ranges::any_of(profile.videoCapabilities,
        [&](const VideoCodecConfig& capability)
        {
            return track.codec == capability.codec &&
                   FitsWithin(track.width, track.height, capability.maxWidth,
                       capability.maxHeight) &&
                   track.fps <= capability.maxFps;
        });
}

bool StreamTargetResolver::CanCopyAudio(const AudioTrack& track, const StreamProfile& profile)
{
    for (const auto& capability : profile.audioCapabilities)
    {
        if (track.codec == capability.codec && track.channels <= capability.maxChannels &&
            track.sampleRate <= capability.maxSampleRate)
        {
            return true;
        }
    }
    return false;
}

StreamTargetResolver::FrameSize StreamTargetResolver::FitInto(
    FrameSize source, int maxWidth, int maxHeight)
{
    const int longSide = std::max(source.width, source.height);
    const int shortSide = std::min(source.width, source.height);
    if (longSide <= 0 || shortSide <= 0)
    {
        return source;
    }

    // Same expression as the ffmpeg scale filter, so the bitrate is picked
    // for the size ffmpeg actually outputs.
    const double scale = std::min({1.0,
        static_cast<double>(std::max(maxWidth, maxHeight)) / longSide,
        static_cast<double>(std::min(maxWidth, maxHeight)) / shortSide});

    const auto evenSide = [scale](int side)
    { return static_cast<int>(std::trunc(side * scale / 2.0)) * 2; };

    return {.width = evenSide(source.width), .height = evenSide(source.height)};
}

StreamTargetResolver::FrameSize StreamTargetResolver::OutputSize(
    const VideoTrack& track, const VideoTargetSpec& spec)
{
    const FrameSize source{.width = track.width, .height = track.height};
    if (spec.copy || (spec.maxWidth == 0 && spec.maxHeight == 0))
    {
        return source;
    }
    return FitInto(source, spec.maxWidth, spec.maxHeight);
}

const EncodingPreset& StreamTargetResolver::LadderPreset(
    const EncodingDefaults& defaults, FrameSize output)
{
    for (const auto presetId : EncodingPresetSetting::kAll)
    {
        const auto& preset = defaults.Preset(presetId);
        if (FitsWithin(output.width, output.height, preset.maxWidth, preset.maxHeight))
        {
            return preset;
        }
    }
    return defaults.Preset(EncodingPresetSetting::kAll.back());
}
