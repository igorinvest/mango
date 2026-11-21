#pragma once

#include "SessionBackend.hpp"

namespace mango
{

// The real SessionBackend: Engine's own methods, plus the change listeners
// of Library, PlaybackHistory and Settings.
class EngineSessionBackend final : public SessionBackend
{
   public:
    explicit EngineSessionBackend(Engine& engine);
    ~EngineSessionBackend() override;

    ServerStatus GetStatus() override;
    std::vector<LibraryEntry> GetLibraryEntries() override;
    std::vector<LibraryFolderEntry> GetLibraryFolders() override;
    LibraryScanInfo GetScanStatus() override;
    TranscodeStatus GetTranscodeStatus() override;
    double SecondsSinceLastStreamRequest() override;
    void SelectFile() override;
    void SelectFolder() override;
    bool AddFile(const std::string& path) override;
    void AddFolder(const std::string& path) override;
    void RescanLibraryFolders() override;

    SettingsState ReadSettings() override;
    std::optional<std::string> ReadSessionSetting(const std::string& key) override;
    void WriteSessionSetting(const std::string& key, const std::string& value) override;
    void SetVideoTranscodeMode(const std::string& mode) override;
    void SetEncodingPreset(const std::string& preset) override;
    void SetAutoplayNextInFolder(bool enabled) override;
    void SetListenOnlyWhileStreaming(bool enabled) override;
    void SetLogDebug(bool enabled) override;
    void SetShowSystemNotifications(bool enabled) override;
    void SetAudioLanguages(const std::vector<std::string>& codes) override;
    void SetSubtitleMode(SubtitleMode mode) override;
    void SetSubtitleLanguages(const std::vector<std::string>& codes) override;
    void ForgetFolderTracks() override;
    void SetChangeListeners(std::function<void()> onLibrary, std::function<void()> onSettings) override;
    void SetFileResetListener(FileResetListener listener) override;
    void SetRemoteNoticeListener(RemoteNoticeListener listener) override;
    bool MediaToolsMissing() override;

    StreamInitiation InitiateStream(const std::string& fastHash, const TrackRequest& tracks, PlaylistType playlist,
        const std::string& deviceKey) override;
    int StreamStartDelayMs() override;
    bool IsStreamStartReady() override;
    void ReportPlaybackProgress(
        const std::string& fastHash, double positionSeconds, int audioTrackId, int subtitleTrackId) override;
    std::string ClassifyCastEnd(const std::string& fastHash, const CastPlaybackStatus& status) override;
    std::string ClassifyLocalEnd(
        const std::string& fastHash, double positionSeconds, const std::string& endReason) override;
    void NotifyStoppedByUser(const std::string& fastHash) override;

    CastDiscoveryResult DiscoverDevices() override;
    CastPlaybackStatus QueryStatus(const CastDevice& device) override;
    std::string Play(const CastDevice& device) override;
    std::string Pause(const CastDevice& device) override;
    std::string Stop(const CastDevice& device) override;
    std::string Seek(const CastDevice& device, double positionSeconds) override;
    std::string Load(const CastDevice& device, const CastLoadRequest& request) override;
    std::string SetSubtitleTrack(const CastDevice& device, const std::string& fastHash, int index) override;

   private:
    Engine& m_engine;
};

}  // namespace mango
