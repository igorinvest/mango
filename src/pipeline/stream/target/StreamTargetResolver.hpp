#pragma once

#include "../../MediaInfo.hpp"
#include "../profiles/StreamProfile.hpp"
#include "StreamTarget.hpp"
#include "StreamTargetSpecs.hpp"

class StreamTargetResolver
{
   public:
    [[nodiscard]] static StreamTargetSpecs Resolve(const std::optional<VideoTrack> videoTrack,
        const std::optional<AudioTrack> audioTrack,
        const std::optional<SubtitleTrack> subtitleTrack, const StreamTarget& target);

    // Whether a width x height frame fits a maxWidth x maxHeight box,
    // comparing long side to long side and short side to short side - so a
    // 1080x1920 portrait frame fits a 1920x1080 box. Shared by the
    // capability check and the preset check so they can't disagree.
    [[nodiscard]] static bool FitsWithin(int width, int height, int maxWidth, int maxHeight);

   private:
    struct FrameSize
    {
        int width{0};
        int height{0};
    };

    [[nodiscard]] static VideoTargetSpec ResolveVideo(const VideoTrack& track,
        const StreamTarget& target, const EncodingDefaults& defaults,
        const EncodingPreset& selected);

    [[nodiscard]] static AudioTargetSpec ResolveAudio(const AudioTrack& track,
        const StreamProfile& profile, const EncodingDefaults& defaults,
        const EncodingPreset& ladderPreset);

    [[nodiscard]] static SubtitleTargetSpec ResolveSubtitle(
        const SubtitleTrack& track, const StreamProfile& profile);

    [[nodiscard]] static bool ShouldCopyVideo(
        const VideoTrack& track, const StreamTarget& target, const EncodingPreset& selected);

    [[nodiscard]] static bool CanCopyVideo(
        const VideoTrack& track, const StreamProfile& profile, const EncodingPreset& selected);

    [[nodiscard]] static bool MatchesCapability(const VideoTrack& track, const StreamProfile& profile);

    [[nodiscard]] static bool CanCopyAudio(const AudioTrack& track, const StreamProfile& profile);

    // The source fitted into the box, long side to long side, keeping the
    // aspect ratio and both sides even - never larger than the source.
    [[nodiscard]] static FrameSize FitInto(FrameSize source, int maxWidth, int maxHeight);

    // Size of the video ffmpeg will output for this spec.
    [[nodiscard]] static FrameSize OutputSize(const VideoTrack& track, const VideoTargetSpec& spec);

    // Smallest preset the output fits in - the presets work as a bitrate
    // ladder, so a 720p output gets 720p bitrates even on the 4k preset.
    [[nodiscard]] static const EncodingPreset& LadderPreset(
        const EncodingDefaults& defaults, FrameSize output);
};
