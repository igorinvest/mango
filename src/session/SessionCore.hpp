#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "CastWorker.hpp"
#include "ChangeNotifier.hpp"
#include "FolderAutoplay.hpp"
#include "NoticeList.hpp"
#include "LibrarySections.hpp"
#include "PlaybackTracker.hpp"
#include "Session.hpp"
#include "SessionThread.hpp"
#include "SleepActivity.hpp"
#include "StreamStarter.hpp"
#include "PendingTrackPicks.hpp"

namespace mango
{

// Session's implementation. Every member function below Post runs on the
// session thread (Session posts each command); only the constructor,
// Shutdown, Post, SetChangedHandler and GetPartJson are called from
// elsewhere. Split over SessionCore.cpp (setup, library, settings,
// devices, publishing) and SessionPlayback.cpp (starting, commands,
// autoplay).
class SessionCore
{
   public:
    SessionCore(std::unique_ptr<SessionBackend> backend, SessionTimings timings);
    ~SessionCore();

    SessionCore(const SessionCore&) = delete;
    SessionCore& operator=(const SessionCore&) = delete;

    void Shutdown();
    void Post(SessionThread::Task task);
    void SetChangedHandler(SessionChangedHandler handler);
    [[nodiscard]] std::string GetPartJson(SessionPart part) const;

    // Setup and housekeeping.
    void Start();
    void ImportAppPreferences(const std::string& lastUsedDeviceId, const std::string& librarySortOrder);

    // Library and settings.
    void AddFile(const std::string& path);
    void AddFolder(const std::string& path);
    void SelectFile();
    void SelectFolder();
    void RescanLibraryFolders();
    void SetSortOrder(const std::string& sortOrder);
    void SetVideoTranscodeMode(const std::string& mode);
    void SetEncodingPreset(const std::string& preset);
    void SetAutoplayNextInFolder(bool enabled);
    void SetListenOnlyWhileStreaming(bool enabled);
    void SetLogDebug(bool enabled);
    void SetShowSystemNotifications(bool enabled);
    void SetAudioLanguages(const std::vector<std::string>& codes);
    void SetSubtitleMode(SubtitleMode mode);
    void SetSubtitleLanguages(const std::vector<std::string>& codes);
    void ForgetFolderTracks();

    // Devices.
    void RefreshDevices();
    void SelectDevice(const std::string& receiverId);
    void SetAirPlayAvailable(bool available);

    // Playback (SessionPlayback.cpp).
    void Play(const std::string& fastHash, const std::string& sectionId, std::optional<double> startSeconds,
        const TrackRequest& chosen);
    void SelectTracks(const std::string& fastHash, int audioTrackId, int subtitleTrackId);
    void SelectAudioTrack(int audioTrackId);
    void SelectSubtitleTrack(int subtitleTrackId);
    void PlayPause();
    void Stop();
    void Seek(double positionSeconds);
    void CancelAutoplay();
    void PlayNextNow();
    void ReportLocalPlayback(double positionSeconds, bool ended);
    void LocalPlayerClosed();

   private:
    // What this session last handed to a player.
    struct Playing
    {
        std::string fastHash;
        int audioTrackId = -1;
        int subtitleTrackId = -1;
        std::string target;  // "chromecast", "airplay" or "" (no player)
    };

    // The stream an AirPlay player in the app plays.
    struct LocalPlayback
    {
        std::string url;
        double startSeconds = 0.0;
        std::uint64_t playbackId = 0;
        double positionSeconds = 0.0;
    };

    // Setup and housekeeping (SessionCore.cpp).
    void ScheduleHousekeeping();
    void Housekeeping();
    void RefreshServer();
    void RefreshTranscode();
    void RefreshSleep();
    void OnLibraryChanged();
    void ScheduleLibraryRefresh();
    void RefreshLibrary();
    void RebuildSections();
    void RefreshSettings();
    // After a preference change: settings and the entries' defaults.
    void RefreshPreferences();

    // Devices (SessionCore.cpp).
    void ApplyDevices(const CastDiscoveryResult& result);
    void KeepOrPickSelection();
    void ApplySelection(const std::string& receiverId);
    [[nodiscard]] std::vector<SessionDevice> Receivers() const;
    [[nodiscard]] std::optional<CastDevice> SelectedCastDevice() const;
    [[nodiscard]] bool IsAirPlaySelected() const;

    // Lookups.
    [[nodiscard]] const LibraryEntry* FindEntry(const std::string& fastHash) const;
    [[nodiscard]] std::optional<std::string> NextFastHash(const std::string& fastHash) const;
    [[nodiscard]] std::string NoNextReason() const;

    // Publishing: each part's JSON, and a change mark when it differs.
    void PublishAll();
    void PublishServer();
    void PublishLibrary();
    void PublishTranscode();
    void PublishDevices();
    void PublishNowPlaying();
    void PublishAutoplay();
    void PublishSettings();
    void PublishNotices();

    // Notices: added to the list (unless a repeat) and logged.
    void ShowNotice(std::string_view code, NoticeParams params, const std::string& fastHash = "");
    void OnFileReset(const std::string& file, const std::string& backup);
    // Shows ffmpegMissing when the backend found no media tools.
    void CheckMediaTools();
    void OnRemoteNotice(const std::string& code, NoticeParams params);
    // {"title": ...} for a file in the library, else nothing.
    [[nodiscard]] NoticeParams TitleParams(const std::string& fastHash) const;
    [[nodiscard]] std::string SelectedDeviceName() const;
    [[nodiscard]] NowPlayingState BuildNowPlaying() const;
    [[nodiscard]] AutoplayState BuildAutoplay() const;
    [[nodiscard]] std::string PlaybackStateName() const;

    // Track picks (SessionPlayback.cpp).
    // What a fresh start of fastHash sends as chosen: chosen's kinds, else
    // the card's pending picks, else nothing (the engine picks).
    [[nodiscard]] TrackRequest ChosenTracks(const std::string& fastHash, const TrackRequest& chosen = {}) const;
    // What entry's card shows: its pending picks over its defaults.
    [[nodiscard]] TrackChoice ShownTracks(const LibraryEntry& entry) const;
    [[nodiscard]] std::vector<TrackChoice> BuildTrackChoices() const;

    // Starting (SessionPlayback.cpp).
    bool StartStream(const LibraryEntry& entry, const TrackRequest& tracks, StartIntent intent,
        std::optional<double> startSeconds = std::nullopt);
    [[nodiscard]] double StartPosition(
        const LibraryEntry& entry, StartIntent intent, std::optional<double> livePosition) const;
    [[nodiscard]] std::optional<double> LivePosition(const std::string& fastHash) const;
    void OnSwitchedInPlace(const StreamRequest& request, const StreamInitiation& result);
    void OnStreamReady(const StreamRequest& request, const StreamInitiation& result);
    void OnStreamFailed(const StreamRequest& request, const std::string& error);
    void NotePlaying(const StreamRequest& request, const StreamInitiation& result, const std::string& target);
    void HandOverLocal(const StreamRequest& request, const StreamInitiation& result);
    void LoadOnTv(const CastDevice& device, const StreamRequest& request, const StreamInitiation& result);
    void OnLoaded(const std::string& result);
    [[nodiscard]] static std::string SubtitleTracksJson(const LibraryEntry& entry, const StreamInitiation& result);

    // Cast commands (SessionPlayback.cpp).
    void RunCommand(const CastDevice& device, std::function<std::string(const CastDevice&)> send);
    void RunSeek(const CastDevice& device, double positionSeconds);
    void OnCommandDone(const CastDevice& device, const std::string& result);
    [[nodiscard]] std::string CommandBlocker() const;

    // The TV's status and the end of playback (SessionPlayback.cpp).
    [[nodiscard]] PollContext MakePollContext() const;
    void OnPolledStatus(const PolledStatus& polled);
    void RestoreFromTv(const CastPlaybackStatus& status);
    void OnEndVerdict(const std::string& endKind, const std::string& fastHash, int token);
    void ShowEndNotice(const std::string& endKind, const std::string& fastHash);

    // Autoplay (SessionPlayback.cpp).
    void RequestPlaylist(const std::string& fastHash, const std::string& sectionId, bool isAutoplay);
    void ScheduleNext(const std::string& fastHash);
    void OnCountdownFired(const std::string& nextFastHash);
    void CancelCountdown();
    void Autoplay(const std::string& fastHash);
    void HandleStreamStartFailure(const std::string& fastHash, const std::string& error);

    std::unique_ptr<SessionBackend> m_backend;
    const SessionTimings m_timings;

    SessionThread m_thread;
    ChangeNotifier m_notifier;
    SessionSnapshots m_snapshots;

    // Blocking work, each kind on its own thread so a slow TV doesn't hold
    // up a stream start or a status poll.
    CastWorker m_streamWorker{"stream"};
    CastWorker m_commandWorker{"cast command"};
    CastWorker m_statusWorker{"cast status"};
    CastWorker m_discoveryWorker{"discovery"};
    CastWorker m_libraryWorker{"library"};

    PendingTrackPicks m_picks;
    FolderAutoplay m_autoplay;
    StreamStarter m_starter;
    PlaybackTracker m_tracker;
    SleepActivity m_sleep;

    bool m_started = false;
    bool m_stopped = false;
    SessionThread::TimerId m_housekeepingTimer = 0;

    ServerStatus m_server;
    TranscodeStatus m_transcode;
    bool m_preventSleep = false;

    LibraryState m_library;
    std::unordered_map<std::string, std::size_t> m_entryIndex;
    LibrarySections::SortOrder m_sortOrder = LibrarySections::SortOrder::DateAdded;
    std::atomic<bool> m_libraryRefreshPending{false};
    SessionThread::Clock::time_point m_lastLibraryRefresh{};

    SettingsState m_settings;

    std::vector<CastDevice> m_castDevices;
    bool m_discovering = false;
    bool m_discoveryFailed = false;
    bool m_discoveredOnce = false;
    bool m_airPlayAvailable = false;
    std::string m_selectedReceiverId;
    std::string m_lastUsedDeviceId;

    Playing m_playing;
    LocalPlayback m_local;
    bool m_loading = false;
    bool m_commandInFlight = false;
    std::optional<double> m_pendingSeek;
    SessionThread::TimerId m_countdownTimer = 0;

    NoticeList m_notices;
};

}  // namespace mango
