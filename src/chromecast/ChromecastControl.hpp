#pragma once

#include <string>
#include <vector>

namespace mango
{

enum class ChromecastCommandResult
{
    Success,          // command sent and a fresh MEDIA_STATUS confirmed it took effect
    SentUnconfirmed,  // command sent, but no confirmation arrived within the timeout
    NoActiveMedia,    // nothing is currently loaded on the device to control
    Unreachable,      // couldn't connect to the device
};

// A subtitle track to declare in a LOAD's tracks list. contentUrl MUST
// already point at a real, existing file — the Default Media Receiver
// eagerly fetches every declared track's contentUrl at LOAD time, not just
// the active one, and a single 404 among them disables subtitle rendering
// entirely (confirmed empirically: even the genuinely-active track fails to
// show if some other declared-but-nonexistent track 404s alongside it). See
// Engine::LoadCastDevice, which is what actually filters candidates down to
// ones that exist before they ever reach here. index doubles as the Cast
// trackId, so it must be the same per-file-unique index used elsewhere
// (mango::TrackInfo.index) — that's also what SetActiveSubtitleTrack's
// subtitleTrackIndex refers to.
struct CastSubtitleTrackRef
{
    int index = 0;
    std::string contentUrl;
};

// Sends playback transport commands to a Chromecast over its CastV2 (TLS)
// control channel. Every command first resolves the receiver's active app
// and media session — required to address the command to the right virtual
// connection — so it only does anything while some media is actually loaded.
class ChromecastControl
{
   public:
    static ChromecastCommandResult Play(const std::string& ipAddress, int port, int timeoutMs = 3000);
    static ChromecastCommandResult Pause(const std::string& ipAddress, int port, int timeoutMs = 3000);

    // Stops the current media session (equivalent to a media-level "stop",
    // not closing the receiver app itself).
    static ChromecastCommandResult Stop(const std::string& ipAddress, int port, int timeoutMs = 3000);

    // Seeks to an absolute position, in seconds from the start of the media.
    static ChromecastCommandResult Seek(
        const std::string& ipAddress, int port, double positionSeconds, int timeoutMs = 3000);

    // Starts playing contentUrl fresh on the Default Media Receiver,
    // launching it if nothing's running (or reusing it if it already is —
    // see CastChannel::LaunchApp). Unlike Play/Pause/Stop/Seek, this
    // doesn't require anything to already be loaded; it's what gets
    // something loaded in the first place, so it never returns
    // NoActiveMedia. contentType is a MIME type (e.g.
    // "application/dash+xml" for the DASH manifests this server produces);
    // title is shown on the receiver's now-playing screen. subtitleTracks
    // is every subtitle track the file has (not just the selected one) —
    // declaring them all upfront is what lets a later subtitle switch use
    // SetActiveSubtitleTrack instead of another Load; activeSubtitleTrackIndex
    // (-1 for none) picks which one starts active. imageUrl (empty for
    // none) is a poster the receiver shows with the title while the stream
    // loads, buffers or is paused. startTimeSeconds resumes
    // playback there instead of from the beginning (see Engine::
    // LoadCastDevice for which position callers pass). A cold app launch
    // can take a few seconds, hence the
    // longer default timeout than the other commands.
    static ChromecastCommandResult Load(const std::string& ipAddress, int port,
        const std::string& contentUrl, const std::string& contentType, const std::string& title,
        const std::vector<CastSubtitleTrackRef>& subtitleTracks, int activeSubtitleTrackIndex,
        const std::string& imageUrl, double startTimeSeconds = 0.0, int timeoutMs = 6000);

    // Switches which declared subtitle track (if any) is showing on an
    // already-loaded session, via EDIT_TRACKS_INFO — no reload, unlike
    // Load, so playback doesn't restart or even pause. subtitleTrackIndex
    // must have been declared in that session's Load() call (pass -1 to
    // turn subtitles off). Requires active media, like Play/Pause/Stop/Seek,
    // so it can't be the first thing sent to a device — Load always is.
    static ChromecastCommandResult SetActiveSubtitleTrack(
        const std::string& ipAddress, int port, int subtitleTrackIndex, int timeoutMs = 3000);
};

}  // namespace mango
