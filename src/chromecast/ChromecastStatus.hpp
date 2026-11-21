#pragma once

#include <string>
#include <vector>

#include "ChromecastDiscovery.hpp"

namespace mango
{

enum class ChromecastPlaybackState
{
    Unreachable,  // couldn't connect, or no reply within the timeout
    Idle,         // connected; no non-idle app is running
    Active,       // a non-idle app is running, but it didn't answer a media status query
    Buffering,
    Playing,
    Paused,
};

struct ChromecastPlaybackStatus
{
    ChromecastPlaybackState state = ChromecastPlaybackState::Unreachable;
    std::string appDisplayName;  // e.g. "YouTube"; empty when idle/unreachable

    // Populated only when a media session is loaded (Playing/Paused/Buffering).
    std::string mediaTitle;       // media.metadata.title, if the sender set one
    std::string contentUrl;       // media.contentId; a URL for browser/Default Media Receiver casts
    double currentTimeSeconds = 0.0;
    double durationSeconds = 0.0;

    // Set only for state Idle with a media session still loaded: why it
    // stopped — "FINISHED", "CANCELLED", "INTERRUPTED" or "ERROR". Short-
    // lived: the receiver soon drops the session.
    std::string idleReason;

    // True when contentUrl's host is one of this machine's addresses — i.e. the
    // TV is playing something served by this app, not an external source
    // (YouTube, another sender, ...).
    bool isFromThisServer = false;
};

// Asks a Chromecast directly, over its CastV2 (TLS) control channel, whether
// it's currently playing anything.
class ChromecastStatus
{
   public:
    // Connects to ipAddress:port, runs the CONNECT/GET_STATUS handshake
    // against the receiver and (if an app is running) its media namespace,
    // and returns what it learned. Blocks the calling thread for up to
    // `timeoutMs`.
    static ChromecastPlaybackStatus Query(const std::string& ipAddress, int port, int timeoutMs = 3000);

    // Queries every device in turn and prints a one-line summary for each.
    static void CheckAllAndPrint(const std::vector<ChromecastDevice>& devices, int timeoutMs = 3000);
};

}  // namespace mango
