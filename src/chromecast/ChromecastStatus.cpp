#include "ChromecastStatus.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>

#include "../server/NetworkUtils.hpp"
#include "CastChannel.hpp"
#include "../log/Log.hpp"

namespace mango
{

namespace
{

ChromecastPlaybackState PlayerStateToEnum(const std::string& playerState)
{
    if (playerState == "PLAYING")
    {
        return ChromecastPlaybackState::Playing;
    }

    if (playerState == "PAUSED")
    {
        return ChromecastPlaybackState::Paused;
    }

    if (playerState == "BUFFERING")
    {
        return ChromecastPlaybackState::Buffering;
    }

    return ChromecastPlaybackState::Idle;
}

// contentUrl is a URL for browser/Default Media Receiver casts (e.g.
// "http://192.168.0.34:8080/stream/<id>/<file>"); native apps like YouTube
// use their own content identifiers instead, which simply won't match here.
std::string ExtractHostFromUrl(const std::string& url)
{
    const size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos)
    {
        return {};
    }

    const size_t hostStart = schemeEnd + 3;
    const size_t hostEnd = url.find_first_of(":/", hostStart);

    return url.substr(hostStart, hostEnd == std::string::npos ? std::string::npos : hostEnd - hostStart);
}

std::string FormatSeconds(double seconds)
{
    const int total = static_cast<int>(seconds);
    std::ostringstream out;
    out << (total / 60) << ':' << std::setw(2) << std::setfill('0') << (total % 60);
    return out.str();
}

std::string DescribeStatus(const ChromecastPlaybackStatus& status)
{
    std::string description;

    switch (status.state)
    {
        case ChromecastPlaybackState::Unreachable:
            return "unreachable";
        case ChromecastPlaybackState::Idle:
            return "idle (no stream)";
        case ChromecastPlaybackState::Active:
            return "app running (" + status.appDisplayName + "), playback state unknown";
        case ChromecastPlaybackState::Buffering:
            description = "buffering (" + status.appDisplayName + ")";
            break;
        case ChromecastPlaybackState::Playing:
            description = "playing (" + status.appDisplayName + ")";
            break;
        case ChromecastPlaybackState::Paused:
            description = "paused (" + status.appDisplayName + ")";
            break;
    }

    if (!status.mediaTitle.empty())
    {
        description += " \"" + status.mediaTitle + "\"";
    }

    if (status.durationSeconds > 0.0)
    {
        description += " [" + FormatSeconds(status.currentTimeSeconds) + "/" + FormatSeconds(status.durationSeconds) + "]";
    }

    description += status.isFromThisServer ? " — from this server" : " — from an external source";

    return description;
}

}  // namespace

ChromecastPlaybackStatus ChromecastStatus::Query(const std::string& ipAddress, int port, int timeoutMs)
{
    ChromecastPlaybackStatus result;

    CastChannel channel;
    if (!channel.Connect(ipAddress, port, timeoutMs))
    {
        return result;  // Unreachable
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    auto media = channel.ResolveActiveMedia(deadline);
    if (!media)
    {
        return result;  // Unreachable: receiver never answered.
    }

    if (!media->hasApp)
    {
        result.state = ChromecastPlaybackState::Idle;
        return result;
    }

    result.state = ChromecastPlaybackState::Active;
    result.appDisplayName = media->appDisplayName;

    if (!media->hasMedia)
    {
        return result;
    }

    result.state = PlayerStateToEnum(media->playerState);
    result.mediaTitle = media->mediaTitle;
    result.contentUrl = media->contentUrl;
    result.currentTimeSeconds = media->currentTime;
    result.durationSeconds = media->duration;
    result.idleReason = media->idleReason;

    if (!result.contentUrl.empty())
    {
        result.isFromThisServer = NetworkUtils::IsThisMachine(ExtractHostFromUrl(result.contentUrl));
    }

    return result;
}

void ChromecastStatus::CheckAllAndPrint(const std::vector<ChromecastDevice>& devices, int timeoutMs)
{
    if (devices.empty())
    {
        Log::Info(LogCategory::Cast, "no devices to check");
        return;
    }

    Log::Info(LogCategory::Cast, "checking playback status of {} device(s)", devices.size());

    for (const auto& device : devices)
    {
        const auto status = Query(device.ipAddress, device.port, timeoutMs);
        Log::Info(LogCategory::Cast, "  {}: {}", device.name, DescribeStatus(status));
    }
}

}  // namespace mango
