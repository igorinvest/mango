#include <optional>
#include "ChromecastControl.hpp"

#include <chrono>
#include <format>
#include <functional>
#include <glaze/glaze.hpp>
#include <vector>

#include "CastChannel.hpp"

namespace mango
{

// JSON shapes for the LOAD request built here. title crosses into this as
// arbitrary file/metadata text (unlike the other commands' numeric-only
// payloads), so this goes through glaze rather than std::format — plain
// string interpolation wouldn't escape a title containing a `"` or `\`.
// Glaze's reflection needs these to have external linkage (matching
// CastChannel.cpp's reply DTOs), so they sit outside the anonymous
// namespace below.
struct CastImageDto
{
    std::string url;
};

struct CastMediaMetadataDto
{
    int metadataType = 0;  // 0 = GenericMediaMetadata
    std::string title;
    // Shown by the Default Media Receiver next to the title while loading,
    // buffering and paused. Left out of the JSON when there's none.
    std::optional<std::vector<CastImageDto>> images;
};

// A single subtitle track declaration. trackId matches CastSubtitleTrackRef's
// index (and hence mango::TrackInfo.index) directly, rather than a fresh
// counter — that's what makes it stable across a later EDIT_TRACKS_INFO
// (see ChromecastControl::SetActiveSubtitleTrack), which addresses tracks by
// the id they were declared with here.
struct CastMediaTrackDto
{
    int trackId = 0;
    std::string type = "TEXT";
    std::string trackContentId;
    std::string trackContentType = "text/vtt";
    std::string subtype = "SUBTITLES";
    std::string name = "Subtitles";
};

struct CastLoadMediaDto
{
    std::string contentId;
    std::string contentType;
    std::string streamType = "BUFFERED";
    CastMediaMetadataDto metadata;
    std::vector<CastMediaTrackDto> tracks;
};

struct CastLoadRequestDto
{
    std::string type = "LOAD";
    CastLoadMediaDto media;
    bool autoplay = true;
    double currentTime = 0.0;
    std::vector<int> activeTrackIds;
    int requestId = 1;
};

namespace
{

// Stock Cast receiver app that plays a raw content URL (no custom receiver
// app registered for this project).
constexpr const char* kDefaultMediaReceiverAppId = "CC1AD845";

ChromecastCommandResult SendCommand(const std::string& ipAddress, int port, int timeoutMs,
    const std::function<std::string(int mediaSessionId)>& buildPayload)
{
    CastChannel channel;
    if (!channel.Connect(ipAddress, port, timeoutMs))
    {
        return ChromecastCommandResult::Unreachable;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    auto media = channel.ResolveActiveMedia(deadline);
    if (!media || !media->hasApp || !media->hasMedia)
    {
        return ChromecastCommandResult::NoActiveMedia;
    }

    const auto reply = channel.SendMediaCommand(media->transportId, buildPayload(media->mediaSessionId), deadline);

    return reply ? ChromecastCommandResult::Success : ChromecastCommandResult::SentUnconfirmed;
}

}  // namespace

ChromecastCommandResult ChromecastControl::Play(const std::string& ipAddress, int port, int timeoutMs)
{
    return SendCommand(ipAddress, port, timeoutMs, [](int mediaSessionId)
        { return std::format(R"({{"type":"PLAY","mediaSessionId":{},"requestId":1}})", mediaSessionId); });
}

ChromecastCommandResult ChromecastControl::Pause(const std::string& ipAddress, int port, int timeoutMs)
{
    return SendCommand(ipAddress, port, timeoutMs, [](int mediaSessionId)
        { return std::format(R"({{"type":"PAUSE","mediaSessionId":{},"requestId":1}})", mediaSessionId); });
}

ChromecastCommandResult ChromecastControl::Stop(const std::string& ipAddress, int port, int timeoutMs)
{
    return SendCommand(ipAddress, port, timeoutMs, [](int mediaSessionId)
        { return std::format(R"({{"type":"STOP","mediaSessionId":{},"requestId":1}})", mediaSessionId); });
}

ChromecastCommandResult ChromecastControl::Seek(
    const std::string& ipAddress, int port, double positionSeconds, int timeoutMs)
{
    return SendCommand(ipAddress, port, timeoutMs, [positionSeconds](int mediaSessionId)
        {
            return std::format(R"({{"type":"SEEK","mediaSessionId":{},"currentTime":{},"requestId":1}})",
                mediaSessionId, positionSeconds);
        });
}

ChromecastCommandResult ChromecastControl::Load(const std::string& ipAddress, int port,
    const std::string& contentUrl, const std::string& contentType, const std::string& title,
    const std::vector<CastSubtitleTrackRef>& subtitleTracks, int activeSubtitleTrackIndex,
    const std::string& imageUrl, double startTimeSeconds, int timeoutMs)
{
    CastChannel channel;
    if (!channel.Connect(ipAddress, port, timeoutMs))
    {
        return ChromecastCommandResult::Unreachable;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    const auto transportId = channel.LaunchApp(kDefaultMediaReceiverAppId, deadline);
    if (!transportId)
    {
        return ChromecastCommandResult::Unreachable;
    }

    CastLoadRequestDto request;
    request.media.contentId = contentUrl;
    request.media.contentType = contentType;
    request.media.metadata.title = title;
    if (!imageUrl.empty())
    {
        request.media.metadata.images = std::vector<CastImageDto>{{.url = imageUrl}};
    }
    request.currentTime = startTimeSeconds;

    for (const auto& subtitleTrack : subtitleTracks)
    {
        CastMediaTrackDto track;
        track.trackId = subtitleTrack.index;
        track.trackContentId = subtitleTrack.contentUrl;
        request.media.tracks.push_back(track);
    }

    if (activeSubtitleTrackIndex >= 0)
    {
        request.activeTrackIds.push_back(activeSubtitleTrackIndex);
    }

    const auto payload = glz::write_json(request);
    if (!payload)
    {
        return ChromecastCommandResult::Unreachable;
    }

    const auto reply = channel.SendMediaCommand(*transportId, *payload, deadline);

    return reply ? ChromecastCommandResult::Success : ChromecastCommandResult::SentUnconfirmed;
}

ChromecastCommandResult ChromecastControl::SetActiveSubtitleTrack(
    const std::string& ipAddress, int port, int subtitleTrackIndex, int timeoutMs)
{
    return SendCommand(ipAddress, port, timeoutMs, [subtitleTrackIndex](int mediaSessionId)
        {
            const std::string activeTrackIds =
                subtitleTrackIndex >= 0 ? std::format("[{}]", subtitleTrackIndex) : "[]";

            return std::format(
                R"({{"type":"EDIT_TRACKS_INFO","mediaSessionId":{},"activeTrackIds":{},"requestId":1}})",
                mediaSessionId, activeTrackIds);
        });
}

}  // namespace mango
