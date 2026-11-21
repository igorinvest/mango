#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Engine.hpp"
#include "pipeline/playback/TrackPreferences.hpp"
#include "SessionState.hpp"

namespace mango
{

// What Session::Load sends to a Chromecast (see Engine::LoadCastDevice).
struct CastLoadRequest
{
    std::string contentUrl;
    std::string contentType;
    std::string title;
    std::string fastHash;
    std::string subtitleTracksJson;
    int activeSubtitleTrackIndex = -1;
    double startTimeSeconds = 0.0;
};

// Everything Session needs from the engine, so the session's flows can be
// tested without a real library, network or ffmpeg. EngineSessionBackend is
// the real one. Calls marked blocking are only made off the session thread
// (on a CastWorker).
class SessionBackend
{
   public:
    virtual ~SessionBackend() = default;

    // Server and library.
    virtual ServerStatus GetStatus() = 0;
    virtual std::vector<LibraryEntry> GetLibraryEntries() = 0;
    virtual std::vector<LibraryFolderEntry> GetLibraryFolders() = 0;
    virtual LibraryScanInfo GetScanStatus() = 0;
    virtual TranscodeStatus GetTranscodeStatus() = 0;
    virtual double SecondsSinceLastStreamRequest() = 0;
    virtual void SelectFile() = 0;
    virtual void SelectFolder() = 0;
    virtual bool AddFile(const std::string& path) = 0;  // blocking
    virtual void AddFolder(const std::string& path) = 0;
    virtual void RescanLibraryFolders() = 0;

    // Settings. librarySortOrder and the last used device are session
    // settings, read and written by key.
    virtual SettingsState ReadSettings() = 0;
    virtual std::optional<std::string> ReadSessionSetting(const std::string& key) = 0;
    virtual void WriteSessionSetting(const std::string& key, const std::string& value) = 0;
    virtual void SetVideoTranscodeMode(const std::string& mode) = 0;
    virtual void SetEncodingPreset(const std::string& preset) = 0;
    virtual void SetAutoplayNextInFolder(bool enabled) = 0;
    virtual void SetListenOnlyWhileStreaming(bool enabled) = 0;
    virtual void SetLogDebug(bool enabled) = 0;
    virtual void SetShowSystemNotifications(bool enabled) = 0;
    virtual void SetAudioLanguages(const std::vector<std::string>& codes) = 0;
    virtual void SetSubtitleMode(SubtitleMode mode) = 0;
    virtual void SetSubtitleLanguages(const std::vector<std::string>& codes) = 0;
    virtual void ForgetFolderTracks() = 0;

    // Called on any thread, possibly with the library's lock held: must
    // only note the change. Pass empty functions to stop.
    virtual void SetChangeListeners(std::function<void()> onLibrary, std::function<void()> onSettings) = 0;

    // Called on any thread for each hand-edited settings file that couldn't
    // be read and was reset (file and backup are file names), also for those
    // reset before it was set. Must only note it. Pass an empty function to
    // stop.
    using FileResetListener = std::function<void(const std::string& file, const std::string& backup)>;
    virtual void SetFileResetListener(FileResetListener listener) = 0;

    // Called on any thread with each remote streaming notice: its catalog
    // code (see NoticeCode in Notices.hpp) and params. Must only note it.
    // Pass an empty function to stop.
    using RemoteNoticeListener = std::function<void(const std::string& code, const NoticeParams& params)>;
    virtual void SetRemoteNoticeListener(RemoteNoticeListener listener) = 0;

    // True when no media tool (ffmpeg) was found, so nothing can be probed
    // or converted. Blocking: waits for the media backend's install stage.
    virtual bool MediaToolsMissing() { return false; }

    // Streams.
    // tracks and deviceKey as in Engine::InitiateStreamJson.
    virtual StreamInitiation InitiateStream(const std::string& fastHash, const TrackRequest& tracks, PlaylistType playlist,
        const std::string& deviceKey) = 0;  // blocking
    virtual int StreamStartDelayMs() = 0;
    virtual bool IsStreamStartReady() = 0;
    virtual void ReportPlaybackProgress(
        const std::string& fastHash, double positionSeconds, int audioTrackId, int subtitleTrackId) = 0;
    virtual std::string ClassifyCastEnd(const std::string& fastHash, const CastPlaybackStatus& status) = 0;
    virtual std::string ClassifyLocalEnd(
        const std::string& fastHash, double positionSeconds, const std::string& endReason) = 0;
    virtual void NotifyStoppedByUser(const std::string& fastHash) = 0;

    // Chromecast I/O, all blocking. Command results as Engine returns
    // them ("Success", "SentUnconfirmed", "NoActiveMedia", "Unreachable",
    // "TrackNotAvailable").
    virtual CastDiscoveryResult DiscoverDevices() = 0;
    virtual CastPlaybackStatus QueryStatus(const CastDevice& device) = 0;
    virtual std::string Play(const CastDevice& device) = 0;
    virtual std::string Pause(const CastDevice& device) = 0;
    virtual std::string Stop(const CastDevice& device) = 0;
    virtual std::string Seek(const CastDevice& device, double positionSeconds) = 0;
    virtual std::string Load(const CastDevice& device, const CastLoadRequest& request) = 0;
    virtual std::string SetSubtitleTrack(const CastDevice& device, const std::string& fastHash, int index) = 0;
};

}  // namespace mango
