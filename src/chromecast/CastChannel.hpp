#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace mango
{

// What's known about the receiver's currently running app and (if any) its
// active media session, as of the last ResolveActiveMedia() call.
struct ActiveMediaSession
{
    bool hasApp = false;    // a non-idle app is running on the receiver
    bool hasMedia = false;  // that app answered a media GET_STATUS with a session

    std::string transportId;
    std::string appDisplayName;

    int mediaSessionId = 0;
    std::string playerState;  // "PLAYING", "PAUSED", "BUFFERING", or "IDLE"
    std::string idleReason;   // why it's IDLE: "FINISHED", "CANCELLED", "INTERRUPTED", "ERROR"; else empty
    std::string mediaTitle;
    std::string contentUrl;
    double currentTime = 0.0;
    double duration = 0.0;
};

// One CastV2 (TLS) session to a Chromecast, plus the session state
// (receiver's active app / media session) that every higher-level
// operation — status query, playback command — needs before it can do
// anything else. Internal to the chromecast/ implementation:
// ChromecastDiscovery, ChromecastStatus and ChromecastControl are the
// public-facing types built on top of this.
class CastChannel
{
   public:
    CastChannel();
    ~CastChannel();

    CastChannel(const CastChannel&) = delete;
    CastChannel& operator=(const CastChannel&) = delete;

    bool Connect(const std::string& ipAddress, int port, int timeoutMs);

    // Resolves the receiver's active app and, if one is running, its
    // current media session. Returns std::nullopt only on a transport-level
    // failure (the receiver didn't answer at all) — an idle receiver with no
    // app running is a successful result with hasApp == false.
    std::optional<ActiveMediaSession> ResolveActiveMedia(std::chrono::steady_clock::time_point deadline);

    // Sends a JSON command to the given app's already-open media namespace
    // virtual connection (opened by a prior ResolveActiveMedia() call on
    // this same channel) and waits for a MEDIA_STATUS reply. Returns its
    // raw JSON payload, or std::nullopt if none arrived before `deadline`.
    std::optional<std::string> SendMediaCommand(
        const std::string& transportId, const std::string& jsonPayload, std::chrono::steady_clock::time_point deadline);

    // Launches appId (e.g. the Default Media Receiver, "CC1AD845") and
    // returns its transportId once the receiver reports it running, opening
    // a virtual connection to it in the process (ready for
    // SendMediaCommand). Chromecast treats LAUNCH as idempotent for an
    // already-running appId — it won't interrupt existing playback, just
    // reports the existing session. Returns std::nullopt on a transport
    // failure or if the app never appeared before `deadline`.
    std::optional<std::string> LaunchApp(
        const std::string& appId, std::chrono::steady_clock::time_point deadline);

   private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}  // namespace mango
