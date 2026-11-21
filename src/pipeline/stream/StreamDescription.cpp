#include "StreamDescription.hpp"

#include <format>

namespace
{

std::string Language(const std::string& language) { return language.empty() ? "und" : language; }

std::string VideoSpec(const VideoTargetSpec& video)
{
    if (video.copy)
    {
        return "video copy";
    }

    auto text = std::format("video {} {}", video.codec, video.bitrate);
    if (video.maxWidth > 0 && video.maxHeight > 0)
    {
        text += std::format(" max {}x{}", video.maxWidth, video.maxHeight);
    }
    if (video.deinterlace)
    {
        text += " deinterlace";
    }
    return text;
}

std::string AudioSpec(const AudioTargetSpec& audio)
{
    return audio.copy ? "audio copy" : std::format("audio {} {}", audio.codec, audio.bitrate);
}

}  // namespace

namespace StreamDescription
{

std::string Tracks(const TrackList& trackList)
{
    std::string text = trackList.videoTrack
        ? std::format("video #{} {}", trackList.videoTrack->index, trackList.videoTrack->codec)
        : "no video";

    text += trackList.audioTrack ? std::format(", audio #{} {} {}", trackList.audioTrack->index,
                                       trackList.audioTrack->codec, Language(trackList.audioTrack->language))
                                 : ", no audio";

    text += trackList.subtitleTrack
        ? std::format(", subtitles #{} {} {}", trackList.subtitleTrack->index, trackList.subtitleTrack->codec,
              Language(trackList.subtitleTrack->language))
        : ", subtitles off";
    return text;
}

std::string Target(const StreamTarget& target)
{
    return std::format("{}, {}, {}", target.playlist == PlaylistType::HLS ? "HLS" : "DASH",
        EncodingPresetSetting::ToString(target.preset), VideoTranscodeModeSetting::ToString(target.mode));
}

std::string Specs(const StreamTargetSpecs& specs)
{
    return VideoSpec(specs.video) + ", " + AudioSpec(specs.audio);
}

std::string Stream(const FileRecord& file, const TrackList& trackList, const StreamTarget& target)
{
    return std::format("{} ({}) — {} — {}", file.libraryName, file.fastHash, Tracks(trackList), Target(target));
}

}  // namespace StreamDescription
