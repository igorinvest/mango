#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include "SessionBackend.hpp"
#include "SessionState.hpp"

namespace mango
{

class SessionCore;

// How often things happen; tests shorten them.
struct SessionTimings
{
    // The change handler is called at most this often.
    std::chrono::milliseconds notifyInterval{100};

    // The selected Chromecast's status.
    std::chrono::milliseconds castPollInterval{2000};

    // Server status, transcode progress, sleep.
    std::chrono::milliseconds housekeepingInterval{1000};

    // Re-reading the library after it changed (a scan changes it per file).
    std::chrono::milliseconds libraryRefreshInterval{500};

    // How often a fresh conversion's head start checks whether it's ready.
    std::chrono::milliseconds streamReadyPollInterval{100};

    // Between one video's end and the next one's start.
    std::chrono::milliseconds autoplayCountdown{5000};

    // The same notice isn't shown again within this.
    std::chrono::milliseconds noticeRepeatWindow{30000};
};

// The app-level state and flows both native apps and the CLI share (see
// docs/scope/shared-app-session-scope.md): the library's sections, the
// devices, starting a stream, the TV's status, cast commands, folder
// autoplay and remembered tracks. Owned by Engine (Engine::GetSession).
//
// Commands return immediately; the work happens on the session's own
// thread. The app learns what changed from the change handler and reads the
// changed parts with GetPartJson (JSON of the structs in SessionState.hpp).
class Session
{
   public:
    explicit Session(std::unique_ptr<SessionBackend> backend, SessionTimings timings = {});
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Reads settings and the library, finds devices and starts polling.
    // Call after Engine::Start. A second call does nothing.
    void Start();

    // Stops polling and every worker; commands are ignored afterwards.
    // Waits for a cast command in flight (up to its timeout).
    void Shutdown();

    void SetChangedHandler(SessionChangedHandler handler);

    // Any thread. Every part has a snapshot from construction on.
    [[nodiscard]] std::string GetPartJson(SessionPart part) const;

    // First start of an app on the session: its own saved last device id
    // and sort order, so nothing is lost. Applied once per installation.
    void ImportAppPreferences(const std::string& lastUsedDeviceId, const std::string& librarySortOrder);

    // Library.
    void AddFile(const std::string& path);
    void AddFolder(const std::string& path);
    void SelectFile();
    void SelectFolder();
    void RescanLibraryFolders();

    // "name" or "dateAdded"; anything else is ignored.
    void SetSortOrder(const std::string& sortOrder);

    // Devices. receiverId as in SessionDevice; "" selects nothing.
    void RefreshDevices();
    void SelectDevice(const std::string& receiverId);

    // macOS: offer AirPlay (played by the app's own player) as a device.
    void SetAirPlayAvailable(bool available);

    // Playback. Play starts fastHash on the selected device, from its resume
    // point unless startSeconds says otherwise. sectionId is the library
    // section it was started from, which becomes the playlist ("" or
    // "manual" for none). Tracks: a kind set in chosen (subtitles -1 for
    // off), else the card's pending pick, else the engine picks (see
    // docs/scope/track-selection-scope.md). Ignored while another start is
    // running.
    void Play(const std::string& fastHash, const std::string& sectionId,
        std::optional<double> startSeconds = std::nullopt, const TrackRequest& chosen = {});

    // A track picked on a card: kept as a pending pick for Play (the kind
    // that differs from what the card shows), and applied right away when
    // that file is what the TV plays.
    void SelectTracks(const std::string& fastHash, int audioTrackId, int subtitleTrackId);

    // The Now Playing bar: an audio track restarts the stream from 0; a
    // subtitle track switches in place when it can, else reloads where the
    // TV is.
    void SelectAudioTrack(int audioTrackId);
    void SelectSubtitleTrack(int subtitleTrackId);

    // One cast command at a time; a second is ignored, except Seek: the
    // latest seek is sent once the command in flight finishes.
    void PlayPause();
    void Stop();
    void Seek(double positionSeconds);

    // Autoplay's countdown: Cancel stops the chain; PlayNextNow starts the
    // next video at once (also without a countdown, as Next).
    void CancelAutoplay();
    void PlayNextNow();

    // AirPlay (macOS): the app's player reports its position every couple
    // of seconds, and once more with ended when it played to the end.
    void ReportLocalPlayback(double positionSeconds, bool ended);

    // The app's player was closed.
    void LocalPlayerClosed();

    // Settings: the engine's setters, which also tell every UI.
    void SetVideoTranscodeMode(const std::string& mode);
    void SetEncodingPreset(const std::string& preset);
    void SetAutoplayNextInFolder(bool enabled);
    void SetListenOnlyWhileStreaming(bool enabled);

    // Debug-level log lines; applies at the next launch.
    void SetLogDebug(bool enabled);

    // Whether the apps send system notifications (see SessionNotice). The
    // engine only keeps the setting.
    void SetShowSystemNotifications(bool enabled);

    // Track preferences (see TrackPreferences). Language codes in any
    // spelling Languages::Normalize takes, best first; unknown ones are
    // dropped. Playback never changes them. Each refreshes the library's
    // pre-selected tracks.
    void SetAudioLanguages(const std::vector<std::string>& codes);
    void SetSubtitleMode(SubtitleMode mode);
    void SetSubtitleLanguages(const std::vector<std::string>& codes);

    // Forgets every folder's remembered track languages.
    void ForgetFolderTracks();

   private:
    std::unique_ptr<SessionCore> m_core;
};

}  // namespace mango
