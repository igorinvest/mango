#pragma once

#include <condition_variable>
#include <format>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "session/PlaybackTracker.hpp"
#include "session/SessionBackend.hpp"

// A SessionBackend with no library, network or ffmpeg behind it: a test sets
// up what it answers and reads back what the session asked for (calls, in
// order). Thread-safe: the session calls it from its workers.
class FakeSessionBackend final : public mango::SessionBackend
{
   public:
    // ---- What a test sets up (under Lock(), or before Start) ----
    std::vector<mango::LibraryEntry> entries;
    std::vector<mango::LibraryFolderEntry> folders;
    // No ffmpeg: the session shows ffmpegMissing after Start.
    bool mediaToolsMissing = false;
    std::vector<mango::CastDevice> devices{{"Living Room", "10.0.0.5", 8009}};
    mango::CastPlaybackStatus tvStatus{.state = "Idle"};
    std::map<std::string, std::string> initiateErrors;  // fastHash -> error
    bool startedNewTranscode = false;
    int streamStartDelayMs = 0;
    bool streamStartReady = false;
    std::string subtitleSwitchResult = "Success";
    std::map<std::string, std::string> sessionSettings;
    mango::SettingsState settings;

    std::unique_lock<std::mutex> Lock() { return std::unique_lock(m_mutex); }

    std::vector<std::string> Calls()
    {
        std::lock_guard lock(m_mutex);
        return m_calls;
    }

    int CountCalls(const std::string& prefix)
    {
        std::lock_guard lock(m_mutex);
        return static_cast<int>(std::ranges::count_if(m_calls, [&](const auto& call) { return call.starts_with(prefix); }));
    }

    // While held, cast commands (Play/Pause/Stop/Seek) wait in the worker.
    void HoldCommands(bool hold)
    {
        {
            std::lock_guard lock(m_mutex);
            m_holdCommands = hold;
        }
        m_released.notify_all();
    }

    // The library changed (a scan, an added file).
    void NotifyLibraryChanged()
    {
        std::function<void()> listener;
        {
            std::lock_guard lock(m_mutex);
            listener = m_onLibrary;
        }
        if (listener)
        {
            listener();
        }
    }

    // Another writer (the CLI, a remote device) changed a setting.
    void ChangeSettingElsewhere(const std::string& preset)
    {
        std::function<void()> listener;
        {
            std::lock_guard lock(m_mutex);
            settings.encodingPreset = preset;
            listener = m_onSettings;
        }
        if (listener)
        {
            listener();
        }
    }

    // ---- SessionBackend ----
    mango::ServerStatus GetStatus() override { return {"10.0.0.2", 8080, true, true}; }
    std::vector<mango::LibraryEntry> GetLibraryEntries() override
    {
        std::lock_guard lock(m_mutex);
        return entries;
    }
    std::vector<mango::LibraryFolderEntry> GetLibraryFolders() override
    {
        std::lock_guard lock(m_mutex);
        return folders;
    }
    mango::LibraryScanInfo GetScanStatus() override { return {}; }
    mango::TranscodeStatus GetTranscodeStatus() override { return {}; }
    double SecondsSinceLastStreamRequest() override { return -1; }
    void SelectFile() override {}
    void SelectFolder() override {}
    bool AddFile(const std::string& path) override { return Record("addFile " + path), true; }
    void AddFolder(const std::string& path) override { Record("addFolder " + path); }
    void RescanLibraryFolders() override { Record("rescan"); }

    mango::SettingsState ReadSettings() override
    {
        std::lock_guard lock(m_mutex);
        auto state = settings;
        if (const auto it = sessionSettings.find("librarySortOrder"); it != sessionSettings.end())
        {
            state.librarySortOrder = it->second;
        }
        return state;
    }
    std::optional<std::string> ReadSessionSetting(const std::string& key) override
    {
        std::lock_guard lock(m_mutex);
        const auto it = sessionSettings.find(key);
        return it == sessionSettings.end() ? std::nullopt : std::optional(it->second);
    }
    void WriteSessionSetting(const std::string& key, const std::string& value) override
    {
        std::lock_guard lock(m_mutex);
        sessionSettings[key] = value;
    }
    void SetVideoTranscodeMode(const std::string& mode) override { Locked([&] { settings.videoTranscodeMode = mode; }); }
    void SetEncodingPreset(const std::string& preset) override { Locked([&] { settings.encodingPreset = preset; }); }
    void SetAutoplayNextInFolder(bool enabled) override { Locked([&] { settings.autoplayNextInFolder = enabled; }); }
    void SetListenOnlyWhileStreaming(bool enabled) override
    {
        Locked([&] { settings.listenOnlyWhileStreaming = enabled; });
    }
    void SetLogDebug(bool enabled) override { Locked([&] { settings.logDebug = enabled; }); }
    void SetShowSystemNotifications(bool enabled) override
    {
        Locked([&] { settings.showSystemNotifications = enabled; });
    }
    void SetFileResetListener(FileResetListener listener) override
    {
        std::lock_guard lock(m_mutex);
        m_onFileReset = std::move(listener);
    }

    bool MediaToolsMissing() override { return mediaToolsMissing; }

    void SetRemoteNoticeListener(RemoteNoticeListener listener) override
    {
        std::lock_guard lock(m_mutex);
        m_onRemoteNotice = std::move(listener);
    }

    // Remote streaming posted a notice.
    void PostRemoteNotice(const std::string& code, const mango::NoticeParams& params)
    {
        RemoteNoticeListener listener;
        {
            std::lock_guard lock(m_mutex);
            listener = m_onRemoteNotice;
        }
        if (listener)
        {
            listener(code, params);
        }
    }

    // A hand-edited settings file couldn't be read.
    void ResetFile(const std::string& file)
    {
        FileResetListener listener;
        {
            std::lock_guard lock(m_mutex);
            listener = m_onFileReset;
        }
        if (listener)
        {
            listener(file, file + ".invalid");
        }
    }

    void SetChangeListeners(std::function<void()> onLibrary, std::function<void()> onSettings) override
    {
        std::lock_guard lock(m_mutex);
        m_onLibrary = std::move(onLibrary);
        m_onSettings = std::move(onSettings);
    }
    void SetAudioLanguages(const std::vector<std::string>& codes) override
    {
        Record(std::format("audioLanguages {}", codes));
        Locked([&] { settings.audioLanguages = codes; });
    }
    void SetSubtitleMode(SubtitleMode mode) override
    {
        Record(std::format("subtitleMode {}", SubtitleModes::ToString(mode)));
        Locked([&] { settings.subtitleMode = std::string(SubtitleModes::ToString(mode)); });
    }
    void SetSubtitleLanguages(const std::vector<std::string>& codes) override
    {
        Record(std::format("subtitleLanguages {}", codes));
        Locked([&] { settings.subtitleLanguages = codes; });
    }
    void ForgetFolderTracks() override { Record("forgetFolderTracks"); }

    // Records "initiate <fastHash> audio=<id|auto> sub=<id|auto>"; plays a
    // chosen track as is, else the entry's default (the engine's pick).
    mango::StreamInitiation InitiateStream(const std::string& fastHash, const TrackRequest& tracks, PlaylistType playlist,
        const std::string& deviceKey) override
    {
        const auto describe = [](std::optional<int> id) { return id ? std::to_string(*id) : std::string("auto"); };
        Record(std::format("initiate {} audio={} sub={}{}{}", fastHash, describe(tracks.audioTrackId),
            describe(tracks.subtitleTrackId), playlist == PlaylistType::HLS ? " hls" : "", deviceKey.empty() ? "" : " device=" + deviceKey));
        std::lock_guard lock(m_mutex);
        mango::StreamInitiation result;
        const auto entry = std::ranges::find(entries, fastHash, &mango::LibraryEntry::fastHash);
        result.audioTrackId = tracks.audioTrackId.value_or(entry != entries.end() ? entry->defaultAudioTrackId : -1);
        result.subtitleTrackId =
            tracks.subtitleTrackId.value_or(entry != entries.end() ? entry->defaultSubtitleTrackId : -1);
        if (const auto it = initiateErrors.find(fastHash); it != initiateErrors.end())
        {
            result.error = it->second;
            return result;
        }
        result.success = true;
        result.streamId = "s-" + fastHash;
        result.serverUrl = "http://10.0.0.2:8080";
        result.streamAddress = result.streamId + "/index.mpd";
        result.startedNewTranscode = startedNewTranscode;
        return result;
    }
    int StreamStartDelayMs() override
    {
        std::lock_guard lock(m_mutex);
        return streamStartDelayMs;
    }
    bool IsStreamStartReady() override
    {
        std::lock_guard lock(m_mutex);
        return streamStartReady;
    }
    void ReportPlaybackProgress(const std::string& fastHash, double positionSeconds, int, int) override
    {
        Record(std::format("report {} {:.0f}", fastHash, positionSeconds));
    }
    // Idle FINISHED is the end, any other idle is a stop.
    std::string ClassifyCastEnd(const std::string&, const mango::CastPlaybackStatus& status) override
    {
        if (mango::PlaybackTracker::IsControllable(status))
        {
            return "Playing";
        }
        if (status.state == "Idle" && !status.idleReason.empty())
        {
            return status.idleReason == "FINISHED" ? "Finished" : "Stopped";
        }
        return "Unknown";
    }
    std::string ClassifyLocalEnd(const std::string&, double, const std::string&) override { return "Finished"; }
    void NotifyStoppedByUser(const std::string& fastHash) override { Record("stoppedByUser " + fastHash); }

    mango::CastDiscoveryResult DiscoverDevices() override
    {
        std::lock_guard lock(m_mutex);
        return {devices, true};
    }
    mango::CastPlaybackStatus QueryStatus(const mango::CastDevice&) override
    {
        std::lock_guard lock(m_mutex);
        return tvStatus;
    }
    std::string Play(const mango::CastDevice&) override { return Command("play"); }
    std::string Pause(const mango::CastDevice&) override { return Command("pause"); }
    std::string Stop(const mango::CastDevice&) override { return Command("stop"); }
    std::string Seek(const mango::CastDevice&, double positionSeconds) override
    {
        return Command(std::format("seek {:.0f}", positionSeconds));
    }
    std::string Load(const mango::CastDevice&, const mango::CastLoadRequest& request) override
    {
        Record(std::format("load {} start={:.0f} sub={}", request.fastHash, request.startTimeSeconds,
            request.activeSubtitleTrackIndex));
        return "Success";
    }
    std::string SetSubtitleTrack(const mango::CastDevice&, const std::string& fastHash, int index) override
    {
        Record(std::format("subtitle {} {}", fastHash, index));
        std::lock_guard lock(m_mutex);
        return subtitleSwitchResult;
    }

   private:
    void Record(std::string call)
    {
        std::lock_guard lock(m_mutex);
        m_calls.push_back(std::move(call));
    }

    template <typename F>
    void Locked(F change)
    {
        std::lock_guard lock(m_mutex);
        change();
    }

    std::string Command(const std::string& name)
    {
        std::unique_lock lock(m_mutex);
        m_released.wait(lock, [this] { return !m_holdCommands; });
        m_calls.push_back(name);
        return "Success";
    }

    std::mutex m_mutex;
    std::condition_variable m_released;
    bool m_holdCommands = false;
    std::vector<std::string> m_calls;
    std::function<void()> m_onLibrary;
    std::function<void()> m_onSettings;
    FileResetListener m_onFileReset;
    RemoteNoticeListener m_onRemoteNotice;
};
