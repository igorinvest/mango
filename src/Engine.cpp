#include "Engine.hpp"

#include <format>
#include <thread>

#include <glaze/glaze.hpp>

#include "CurrentConversion.hpp"
#include "EngineRemote.hpp"
#include "EngineRemoteHost.hpp"
#include "EngineStreams.hpp"
#include "ShutdownCoordinator.hpp"
#include "chromecast/CastStatusLog.hpp"
#include "chromecast/ChromecastControl.hpp"
#include "chromecast/ChromecastDiscovery.hpp"
#include "chromecast/ChromecastStatus.hpp"
#include "configuration/AutoplaySetting.hpp"
#include "configuration/EncodingPreset.hpp"
#include "configuration/ListenModeSetting.hpp"
#include "configuration/Languages.hpp"
#include "configuration/Settings.hpp"
#include "configuration/StreamStartDelay.hpp"
#include "configuration/TrackPreferencesStore.hpp"
#include "configuration/VideoTranscodeMode.hpp"
#include "file/File.hpp"
#include "log/Log.hpp"
#include "media/MediaBackend.hpp"
#include "pipeline/playback/FolderTrackMemory.hpp"
#include "pipeline/library/Library.hpp"
#include "pipeline/playback/PlaybackEndDetector.hpp"
#include "pipeline/playback/PlaybackHistory.hpp"
#include "pipeline/StreamId.hpp"
#include "pipeline/stream/StreamManager.hpp"
#include "pipeline/StreamPipeline.hpp"
#include "pipeline/playback/TrackSelector.hpp"
#include "server/StreamingSession.hpp"
#include "server/StreamActivity.hpp"
#include "session/EngineSessionBackend.hpp"
#include "remote/RemoteCore.hpp"
#include "remote/forwarding/RemoteLinkHandler.hpp"
#include "remote/RemoteStreaming.hpp"
#include "session/Session.hpp"
#include "pipeline/stream/profiles/DeviceProfiles.hpp"
#include "pipeline/stream/subtitles/SubtitleExtractor.hpp"
#include "pipeline/stream/profiles/SupportedFormats.hpp"

namespace mango
{

namespace
{

// What Play would start entry with when nothing is picked (see
// TrackSelector) — so a card shows it. -1 when the list is empty, and for
// subtitles also when they'd be off.
void ApplyDefaultTracks(
    LibraryEntry& entry, const FileRecord& file, const PlaybackRecord* played, const TrackPreferences& preferences)
{
    const TrackContext context{.preferences = preferences,
        .played = played ? std::optional(*played) : std::nullopt,
        .folder = FolderTrackMemory::GetInstance().Get(file.filePath)};
    const auto selection = TrackSelector::SelectTracks(file, {}, context);

    entry.defaultAudioTrackId = selection.audioTrackId.value_or(-1);

    // A track left out of the list (an external file that failed to
    // convert) can't be shown pre-selected.
    const auto subtitle = selection.subtitleTrackId.value_or(-1);
    entry.defaultSubtitleTrackId =
        std::ranges::contains(entry.subtitleTracks, subtitle, &TrackInfo::index) ? subtitle : -1;
}

const char* PlaylistName(PlaylistType playlist)
{
    switch (playlist)
    {
        case PlaylistType::DASH: return "DASH";
        case PlaylistType::HLS: return "HLS";
    }
    return "unknown";
}

std::string ToString(ChromecastPlaybackState state)
{
    switch (state)
    {
        case ChromecastPlaybackState::Idle: return "Idle";
        case ChromecastPlaybackState::Active: return "Active";
        case ChromecastPlaybackState::Buffering: return "Buffering";
        case ChromecastPlaybackState::Playing: return "Playing";
        case ChromecastPlaybackState::Paused: return "Paused";
        case ChromecastPlaybackState::Unreachable: break;
    }

    return "Unreachable";
}

// The receiver keeps the duration it read from the manifest at LOAD — while
// converting, that's only what was muxed so far (or nothing, for a dynamic
// manifest), and it isn't updated once the manifest turns final. Once this
// server's conversion of the casted file has finished, the library's own
// duration is the real one, and the whole file can be seeked into.
std::optional<double> FinishedConversionDuration(const ChromecastPlaybackStatus& status)
{
    auto& manager = StreamManager::GetInstance();
    if (!status.isFromThisServer || !manager.IsCurrentTranscodeCompleted())
    {
        return std::nullopt;
    }

    const auto streamId = StreamId::FromStreamUrl(status.contentUrl);
    const auto record = streamId ? manager.FindStream(*streamId) : std::nullopt;
    if (!record || record->mediaInfo.durationSec <= 0)
    {
        return std::nullopt;
    }

    return static_cast<double>(record->mediaInfo.durationSec);
}

// The fastHash of the file behind a ".../stream/<streamId>/<file>" content
// URL this server handed to the TV, while that stream is still the current
// one; empty for anything else.
std::string StreamFastHash(const ChromecastPlaybackStatus& status)
{
    const auto streamId = status.isFromThisServer ? StreamId::FromStreamUrl(status.contentUrl) : std::nullopt;
    const auto record = streamId ? StreamManager::GetInstance().FindStream(*streamId) : std::nullopt;
    return record ? record->fastHash : std::string();
}

std::string ToString(ChromecastCommandResult result)
{
    switch (result)
    {
        case ChromecastCommandResult::Success: return "Success";
        case ChromecastCommandResult::SentUnconfirmed: return "SentUnconfirmed";
        case ChromecastCommandResult::NoActiveMedia: return "NoActiveMedia";
        case ChromecastCommandResult::Unreachable: break;
    }

    return "Unreachable";
}

std::string DeviceName(const std::string& ipAddress, int port) { return std::format("{}:{}", ipAddress, port); }

// Sends nothing itself: logs the command's result, then returns it.
std::string LogCastCommand(
    const std::string& ipAddress, int port, const std::string& command, ChromecastCommandResult result)
{
    auto text = ToString(result);
    CastStatusLog::GetInstance().OnCommand(DeviceName(ipAddress, port), command, text);
    return text;
}

std::string DescribeSubtitleTrack(int index) { return index >= 0 ? std::to_string(index) : "off"; }

// A request's track for the log: "auto" when the engine picks.
std::string DescribeRequested(std::optional<int> index, bool subtitle)
{
    if (!index) return "auto";
    return subtitle ? DescribeSubtitleTrack(*index) : std::to_string(*index);
}

}  // namespace

namespace
{

// kind as the apps get it, after telling session when it ends the current
// stream.
std::string NoteIfEnded(StreamingSession& session, const std::string& fastHash, PlaybackEndKind kind)
{
    // Only the current stream's end ends the session: an older file's
    // late verdict says nothing about what plays now.
    const auto current = StreamManager::GetInstance().GetCurrentFileRecord();
    const bool ended = kind == PlaybackEndKind::Finished || kind == PlaybackEndKind::Stopped;
    if (ended && current && current->fastHash == fastHash)
    {
        session.NotePlaybackEnded();
    }
    return PlaybackEndDetector::ToString(kind);
}

// A TV still playing or paused on this server's stream keeps its session
// open.
void NoteSessionActivity(StreamingSession& session, const CastPlaybackStatus& status)
{
    // fastHash is only set for this server's current stream.
    const bool active = status.state == "Playing" || status.state == "Paused" || status.state == "Buffering";
    if (active && !status.fastHash.empty())
    {
        session.NoteStillPlaying();
    }
}

// A video evicted from the library takes its playback history with it.
void ConnectLibraryToHistory()
{
    // PlaybackHistory before Library: on first launch after an update it
    // imports the playback history library.json used to hold, which
    // Library's first save would otherwise drop.
    auto& history = PlaybackHistory::GetInstance();

    Library::GetInstance().SetFilesRemovedListener(
        [&history](const std::vector<std::string>& fastHashes)
        {
            for (const auto& fastHash : fastHashes)
            {
                history.Remove(fastHash);
            }
        });
}

}  // namespace

struct Engine::Impl
{
    StreamingSession session;
    bool started = false;

    // Created by the Engine constructor: it calls back into the engine.
    std::unique_ptr<Session> appSession;

    RemoteStreaming remote;

    // Engine calls for entries of a paired peer's library.
    EngineRemote remoteCalls{session, remote};
    CurrentConversion conversion{
        [this](const std::string& entryId) { return remoteCalls.TranscodeStatusOf(entryId); }};
};

Engine::Engine() : impl(std::make_unique<Impl>())
{
    ConnectLibraryToHistory();

    impl->appSession = std::make_unique<Session>(std::make_unique<EngineSessionBackend>(*this));

    // The TV fetches a remote stream from this server's /remote/... route.
    const remote::RemoteLinkHandler handler(
        [&remote = impl->remote](const std::string& peerId) { return remote.Core().FindClient(peerId); });
    impl->session.SetRemoteHandler(
        [handler](const httplib::Request& req, httplib::Response& res) { handler.Handle(req, res); });
}

Engine::~Engine()
{
    // Its workers call into the engine: before anything else goes.
    impl->appSession.reset();
    impl->remote.Shutdown();
    Stop();
    MediaBackends::Get().StopProbe();
    PlaybackHistory::GetInstance().Flush();
}

void Engine::Start()
{
    ShutdownCoordinator::InstallSignalHandler();

    if (impl->started)
    {
        return;
    }
    impl->started = true;

    Log::Info(LogCategory::Engine, "starting");

    // Finds ffmpeg and tests the encoders while the user picks a file.
    MediaBackends::Get().StartProbe();

    // Fire-and-forget: a one-shot network probe, not part of the server's
    // own lifecycle, so it isn't tracked/joined alongside serverThread.
    std::thread(
        []()
        {
            const auto discovery = ChromecastDiscovery::DiscoverAndPrint();
            ChromecastStatus::CheckAllAndPrint(discovery.devices);
        })
        .detach();

    impl->session.Enable(ListenModeSetting::GetListenOnlyWhileStreaming());
    impl->appSession->Start();
    impl->remote.Core().SetHost(std::make_shared<EngineRemoteHost>(*this, impl->session, impl->conversion));
    impl->remote.Start();
}

void Engine::Stop()
{
    if (!impl->started)
    {
        return;
    }
    impl->started = false;

    Log::Info(LogCategory::Engine, "stopping the server");
    impl->session.Disable();
}

ServerStatus Engine::GetStatus() const { return impl->session.GetStatus(); }

void Engine::SetOpenFileHandler(CallbackRegister::OpenFileHandler handler)
{
    CallbackRegister::GetInstance().SetOpenFileHandler(std::move(handler));
}

void Engine::SetOpenFolderHandler(CallbackRegister::OpenFolderHandler handler)
{
    CallbackRegister::GetInstance().SetOpenFolderHandler(std::move(handler));
}

void Engine::SetSecretStoreHandlers(CallbackRegister::SecretStoreHandlers handlers)
{
    CallbackRegister::GetInstance().SetSecretStoreHandlers(std::move(handlers));
}

std::vector<std::string> Engine::GetSecurityBookmarks() const
{
    std::vector<std::string> bookmarks;

    for (const auto& file : Library::GetInstance().GetAllCachedFiles())
    {
        if (file.securityBookmark)
        {
            bookmarks.push_back(*file.securityBookmark);
        }
    }

    // Covers every file a scan found under the folder — those have no
    // bookmark of their own.
    for (const auto& folder : Library::GetInstance().GetFolders())
    {
        if (folder.securityBookmark)
        {
            bookmarks.push_back(*folder.securityBookmark);
        }
    }

    return bookmarks;
}

void Engine::PruneMissingLibraryFiles()
{
    Library::GetInstance().RemoveMissingFiles();
    Library::GetInstance().RescanFolders();
}

void Engine::SelectFile() { Library::GetInstance().SelectFile(); }

void Engine::SelectFolder() { Library::GetInstance().SelectFolder(); }

bool Engine::AddFile(const std::string& filePath)
{
    return Library::GetInstance().AddFile(File::FromUtf8(filePath), "");
}

void Engine::AddFolder(const std::string& folderPath)
{
    Library::GetInstance().AddFolder(File::FromUtf8(folderPath), "");
}

void Engine::RescanLibraryFolders() { Library::GetInstance().RescanFolders(); }

std::string Engine::GetLibraryEntriesJson() const
{
    std::vector<LibraryEntry> entries;
    const auto history = PlaybackHistory::GetInstance().GetAll();
    const auto preferences = TrackPreferencesStore::Get();

    for (const auto& file : Library::GetInstance().GetAllCachedFiles())
    {
        LibraryEntry entry;
        entry.fastHash = file.fastHash;
        entry.title = file.libraryName;
        entry.filePath = File::ToUtf8(file.filePath);

        if (const auto video = file.find_video_by_index())
        {
            entry.videoWidth = video->width;
            entry.videoHeight = video->height;
            entry.videoCodec = video->codec;
        }

        for (const auto& audio : file.mediaInfo.audios)
        {
            entry.audioTracks.push_back(TrackInfo{audio.index, audio.language, audio.title, false, {}});
        }

        // External files after the built-in tracks, as for subtitles.
        for (const auto& external : file.externalAudios)
        {
            entry.audioTracks.push_back(
                TrackInfo{external.index, external.language, "", true, external.title});
        }

        // Bitmap tracks (PGS, DVD) are left out: they can't be converted
        // to WebVTT, so no receiver could show them, and picking one used
        // to break playback (see LoadCastDevice).
        for (const auto& subtitle : file.mediaInfo.subtitles)
        {
            if (SubtitleExtractor::IsTextCodec(subtitle.codec))
            {
                entry.subtitleTracks.push_back(
                    TrackInfo{subtitle.index, subtitle.language, subtitle.title, false, {}});
            }
        }

        // External files after the built-in tracks. One that failed to
        // convert is left out, like a bitmap track.
        for (const auto& external : file.externalSubtitles)
        {
            if (SubtitleExtractor::CanExtract(file, external.index))
            {
                entry.subtitleTracks.push_back(
                    TrackInfo{external.index, external.language, "", true, external.title});
            }
        }

        const auto historyIt = history.find(file.fastHash);
        const PlaybackRecord* played = historyIt != history.end() ? &historyIt->second : nullptr;
        ApplyDefaultTracks(entry, file, played, preferences);

        if (played)
        {
            entry.lastPositionSeconds = played->positionSeconds;
            entry.resumeStartSeconds = played->ResumeStartSeconds();
            entry.isFullyWatched = played->completed;
            entry.lastPlayedAtUnixSeconds = played->updatedAtUnixSeconds;
        }

        entry.durationSeconds = static_cast<double>(file.mediaInfo.durationSec);
        entry.addedThisSession = file.addedThisSession;
        entry.addedAtUnixSeconds = file.addedAtUnixSeconds;
        entry.addedManually = file.addedManually;

        for (const auto& location : file.folderLocations)
        {
            const auto slash = location.relativePath.rfind('/');
            entry.folderLocations.push_back(LibraryEntryLocation{location.folderId,
                location.relativePath,
                slash == std::string::npos ? "" : location.relativePath.substr(0, slash)});
        }

        if (file.coverFileName)
        {
            entry.coverPath = File::ToUtf8(file.getMetadataFolder() / *file.coverFileName);
        }

        entries.push_back(std::move(entry));
    }

    const auto json = glz::write_json(entries);
    return json ? *json : "[]";
}

std::string Engine::GetLibraryFoldersJson() const
{
    std::vector<LibraryFolderEntry> entries;

    for (const auto& folder : Library::GetInstance().GetFolders())
    {
        auto name = File::ToUtf8(folder.rootPath.filename());
        if (name.empty())
        {
            name = File::ToUtf8(folder.rootPath);
        }

        entries.push_back(LibraryFolderEntry{
            folder.folderId, std::move(name), File::ToUtf8(folder.rootPath), folder.addedAtUnixSeconds});
    }

    const auto json = glz::write_json(entries);
    return json ? *json : "[]";
}

std::string Engine::GetLibraryScanStatusJson() const
{
    const auto json = glz::write_json(Library::GetInstance().GetScanStatus());
    return json ? *json : R"({"scanning":false})";
}

std::string Engine::DiscoverCastDevicesJson(int timeoutMs) const
{
    const auto discovery = ChromecastDiscovery::DiscoverAndPrint(timeoutMs);

    CastDiscoveryResult payload;
    payload.succeeded = discovery.succeeded;

    std::vector<SeenDevice> seen;
    for (const auto& device : discovery.devices)
    {
        payload.devices.push_back(CastDevice{device.name, device.ipAddress, device.port, device.castId});
        seen.push_back(SeenDevice{DeviceProfiles::ChromecastKey(device.castId), device.name});
    }
    DeviceProfiles::Remember(seen);

    const auto json = glz::write_json(payload);
    return json ? *json : R"({"devices":[],"succeeded":false})";
}

std::string Engine::QueryCastPlaybackStatusJson(
    const std::string& ipAddress, int port, int timeoutMs) const
{
    const auto status = ChromecastStatus::Query(ipAddress, port, timeoutMs);

    CastPlaybackStatus payload;
    payload.state = ToString(status.state);
    payload.appDisplayName = status.appDisplayName;
    payload.mediaTitle = status.mediaTitle;
    payload.currentTimeSeconds = status.currentTimeSeconds;
    payload.durationSeconds = FinishedConversionDuration(status).value_or(status.durationSeconds);
    payload.tvDurationSeconds = status.durationSeconds;
    payload.isFromThisServer = status.isFromThisServer;
    payload.hasMediaInfo = !status.contentUrl.empty();
    payload.fastHash = StreamFastHash(status);
    if (payload.fastHash.empty() && status.isFromThisServer)
    {
        payload.fastHash = impl->remoteCalls.EntryForUrl(status.contentUrl);
    }
    payload.idleReason = status.idleReason;

    CastStatusLog::GetInstance().OnStatus(DeviceName(ipAddress, port), payload);
    NoteSessionActivity(impl->session, payload);

    const auto json = glz::write_json(payload);
    return json ? *json : "{}";
}

std::string Engine::PlayCastDevice(const std::string& ipAddress, int port, int timeoutMs) const
{
    return LogCastCommand(ipAddress, port, "Play", ChromecastControl::Play(ipAddress, port, timeoutMs));
}

std::string Engine::PauseCastDevice(const std::string& ipAddress, int port, int timeoutMs) const
{
    return LogCastCommand(ipAddress, port, "Pause", ChromecastControl::Pause(ipAddress, port, timeoutMs));
}

std::string Engine::StopCastDevice(const std::string& ipAddress, int port, int timeoutMs) const
{
    return LogCastCommand(ipAddress, port, "Stop", ChromecastControl::Stop(ipAddress, port, timeoutMs));
}

std::string Engine::SeekCastDevice(
    const std::string& ipAddress, int port, double positionSeconds, int timeoutMs) const
{
    // The user moved playback — don't hold back the next reports waiting
    // for a resume point they've chosen to leave.
    PlaybackHistory::GetInstance().ClearPendingResume();

    return LogCastCommand(ipAddress, port, std::format("Seek to {:.1f}", positionSeconds),
        ChromecastControl::Seek(ipAddress, port, positionSeconds, timeoutMs));
}


namespace
{

// Before a LOAD of a local file: keeps the subtitle tracks this server can
// serve, starts the resume window, and returns the cover's URL.
std::string PrepareLocalLoad(const std::string& fastHash, const std::string& contentUrl,
    std::vector<CastSubtitleTrackRef>& subtitleTracks, int& activeSubtitleTrackIndex, double startTimeSeconds)
{
    std::erase_if(subtitleTracks, [&fastHash](const CastSubtitleTrackRef& track)
        { return !EngineStreams::SubtitleTrackCanBeServed(fastHash, track.index); });

    // The receiver rejects a LOAD whose activeTrackIds names a track it
    // wasn't given, so playback would stop instead of just lacking
    // subtitles - play without them instead.
    if (activeSubtitleTrackIndex >= 0 &&
        !std::ranges::contains(subtitleTracks, activeSubtitleTrackIndex, &CastSubtitleTrackRef::index))
    {
        Log::Warning(LogCategory::Subtitles, "subtitle track {} can't be served for {}; loading without subtitles",
            activeSubtitleTrackIndex, fastHash);
        activeSubtitleTrackIndex = -1;
    }

    PlaybackHistory::GetInstance().BeginSession(fastHash, startTimeSeconds);
    return EngineStreams::CoverUrl(contentUrl, fastHash);
}

}  // namespace

std::string Engine::LoadCastDevice(const std::string& ipAddress, int port,
    const std::string& contentUrl, const std::string& contentType, const std::string& title,
    const std::string& fastHash, const std::string& subtitleTracksJson, int activeSubtitleTrackIndex,
    double startTimeSeconds, int timeoutMs) const
{
    std::vector<CastSubtitleTrackRef> subtitleTracks;
    // Malformed/empty input just means "no subtitle tracks to declare".
    if (glz::read_json(subtitleTracks, subtitleTracksJson))
    {
        subtitleTracks.clear();
    }

    const auto coverUrl = EngineRemote::Is(fastHash)
                              ? impl->remoteCalls.PrepareLoad(
                                    fastHash, contentUrl, subtitleTracks, activeSubtitleTrackIndex, startTimeSeconds)
                              : PrepareLocalLoad(
                                    fastHash, contentUrl, subtitleTracks, activeSubtitleTrackIndex, startTimeSeconds);

    const auto result = ChromecastControl::Load(ipAddress, port, contentUrl, contentType, title, subtitleTracks,
        activeSubtitleTrackIndex, coverUrl, startTimeSeconds, timeoutMs);
    return LogCastCommand(ipAddress, port,
        std::format("LOAD {} ({}) start={:.1f} subtitle={} declared={}", title, fastHash, startTimeSeconds,
            DescribeSubtitleTrack(activeSubtitleTrackIndex), subtitleTracks.size()),
        result);
}

std::string Engine::SetCastSubtitleTrack(const std::string& ipAddress, int port,
    const std::string& fastHash, int subtitleTrackIndex, int timeoutMs) const
{
    const auto command = std::format("subtitle track {} for {}", DescribeSubtitleTrack(subtitleTrackIndex), fastHash);
    const auto servable = [&] {
        return EngineRemote::Is(fastHash) ? impl->remoteCalls.CanServeSubtitle(fastHash, subtitleTrackIndex)
                                          : EngineStreams::SubtitleTrackCanBeServed(fastHash, subtitleTrackIndex);
    };
    if (subtitleTrackIndex >= 0 && !servable())
    {
        Log::Info(LogCategory::Cast, "{} not sent to {}: TrackNotAvailable", command, DeviceName(ipAddress, port));
        return "TrackNotAvailable";
    }

    return LogCastCommand(ipAddress, port, command,
        ChromecastControl::SetActiveSubtitleTrack(ipAddress, port, subtitleTrackIndex, timeoutMs));
}

std::string Engine::InitiateStreamJson(
    const std::string& fastHash, const TrackRequest& tracks, PlaylistType playlist, const std::string& deviceKey) const
{
    Log::Info(LogCategory::Stream, "initiate {} audio={} subtitle={} {}{}{}", fastHash,
        DescribeRequested(tracks.audioTrackId, false), DescribeRequested(tracks.subtitleTrackId, true),
        PlaylistName(playlist), deviceKey.empty() ? "" : " for ", deviceKey);

    if (EngineRemote::Is(fastHash))
    {
        impl->conversion.StartedRemote(fastHash);
        const auto remotePayload =
            impl->remoteCalls.Initiate(fastHash, tracks, playlist, deviceKey);
        Log::Info(LogCategory::Stream, "remote stream for {}: {}", fastHash,
            remotePayload.success ? remotePayload.serverUrl + "/stream/" + remotePayload.streamAddress
                                  : remotePayload.error);
        return glz::write_json(remotePayload).value_or("{}");
    }

    impl->conversion.StartedLocal();
    StreamInitiation payload;
    PlaybackEndDetector::GetInstance().BeginSession(fastHash);

    // Opened before the stream starts, so its URLs carry the address and
    // port actually listened on.
    const auto serverUrl = impl->session.Open();
    if (serverUrl)
    {
        payload.serverUrl = *serverUrl;
        EngineStreams::Start(payload, fastHash, tracks, StreamTargets::Resolve(playlist, deviceKey));
    }
    else
    {
        payload.error = "noNetwork";
    }

    if (payload.success)
    {
        Log::Info(LogCategory::Stream, "{} conversion for {}: {} audio={} subtitle={}",
            payload.startedNewTranscode ? "started a new" : "reused the running", fastHash, payload.streamAddress,
            payload.audioTrackId, DescribeSubtitleTrack(payload.subtitleTrackId));
    }
    else
    {
        Log::Error(LogCategory::Stream, "no stream started for {}: {}", fastHash, payload.error);
        // Nothing will play: let the session close like after an end.
        impl->session.NotePlaybackEnded();
    }

    const auto json = glz::write_json(payload);
    return json ? *json : "{}";
}

std::string Engine::GetLanguagesJson() const { return Languages::GetLanguagesJson(); }

std::string Engine::GetVideoTranscodeMode() const
{
    return VideoTranscodeModeSetting::ToString(VideoTranscodeModeSetting::Get());
}

void Engine::SetVideoTranscodeMode(const std::string& mode)
{
    if (const auto parsed = VideoTranscodeModeSetting::FromString(mode))
    {
        VideoTranscodeModeSetting::Set(*parsed);
    }
}

std::string Engine::GetEncodingPreset() const
{
    return EncodingPresetSetting::ToString(EncodingPresetSetting::Get());
}

void Engine::SetEncodingPreset(const std::string& preset)
{
    if (const auto parsed = EncodingPresetSetting::FromString(preset))
    {
        EncodingPresetSetting::Set(*parsed);
    }
}

std::string Engine::GetSupportedFormatsJson(PlaylistType playlist) const
{
    auto info = SupportedFormats::Describe(StreamProfiles::Get(playlist));
    info.conversion = MediaBackends::Get().ConversionOptions(playlist);

    // Not-supported codecs must come out as "maxResolution": null, not be
    // dropped.
    const auto json = glz::write<glz::opts{.skip_null_members = false}>(info);
    return json ? *json : "{}";
}

std::vector<std::string> Engine::GetMediaTools() const
{
    return MediaBackends::Get().DescribeTools();
}

void Engine::SetVideoCodecMaxResolution(
    PlaylistType playlist, const std::string& codec, const std::optional<std::string>& maxResolution)
{
    if (!SupportedFormats::IsListedVideoCodec(codec))
    {
        return;
    }

    std::optional<EncodingPresetId> preset;
    if (maxResolution)
    {
        preset = EncodingPresetSetting::FromString(*maxResolution);
        if (!preset)
        {
            return;
        }
    }

    Log::Info(LogCategory::Settings, "{} supported formats: video {} max {}", PlaylistName(playlist), codec,
        maxResolution.value_or("not supported"));
    StreamProfiles::SetVideoCodecMaxResolution(playlist, codec, preset);
}

void Engine::SetAudioCodecSupported(PlaylistType playlist, const std::string& codec, bool supported)
{
    if (SupportedFormats::IsListedAudioCodec(codec))
    {
        Log::Info(LogCategory::Settings, "{} supported formats: audio {} {}", PlaylistName(playlist), codec,
            supported ? "supported" : "not supported");
        StreamProfiles::SetAudioCodecSupported(playlist, codec, supported);
    }
}

void Engine::SetRotationSupported(PlaylistType playlist, bool supported)
{
    Log::Info(LogCategory::Settings, "{} supported formats: rotation {}", PlaylistName(playlist),
        supported ? "supported" : "not supported");
    StreamProfiles::SetRotationSupported(playlist, supported);
}

void Engine::RestoreSupportedFormatDefaults(PlaylistType playlist)
{
    Log::Info(LogCategory::Settings, "{} supported formats: restored defaults", PlaylistName(playlist));
    StreamProfiles::RestoreDefaults(playlist);
}

int Engine::GetStreamStartDelayMs() const
{
    return static_cast<int>(StreamStartDelaySetting::Get().count());
}

// A peer's conversion counts once it reported it: until then a remote
// start gets its full head start (see GetStreamStartDelayMs).
bool Engine::IsStreamStartReady() const { return impl->conversion.IsReady(); }

void Engine::ReportPlaybackProgress(
    const std::string& fastHash, double positionSeconds, int audioTrackId, int subtitleTrackId)
{
    if (EngineRemote::Is(fastHash))
    {
        impl->remoteCalls.ReportProgress(fastHash, positionSeconds, audioTrackId, subtitleTrackId);
        return;
    }
    PlaybackHistory::GetInstance().Report(fastHash, positionSeconds, audioTrackId, subtitleTrackId);
}

double Engine::GetTranscodeProgressPercent() const { return impl->conversion.Status().percent; }

std::string Engine::GetTranscodeStatusJson() const
{
    const auto json = glz::write_json(impl->conversion.Status());
    return json ? *json : R"({"fastHash":"","percent":-1})";
}

double Engine::GetSecondsSinceLastStreamRequest() const
{
    const auto elapsed = StreamActivity::TimeSinceLastRequest();
    return elapsed ? std::chrono::duration<double>(*elapsed).count() : -1.0;
}

std::string Engine::ClassifyCastPlaybackEnd(
    const std::string& fastHash, const std::string& statusJson) const
{
    CastPlaybackStatus status;
    constexpr glz::opts lenientOpts{.error_on_unknown_keys = false};
    if (glz::read<lenientOpts>(status, statusJson))
    {
        return PlaybackEndDetector::ToString(PlaybackEndKind::Unknown);
    }

    const auto end = PlaybackEndDetector::FromCastStatus(
        status.state, status.idleReason,
        status.hasMediaInfo ? std::optional(status.isFromThisServer) : std::nullopt, status.currentTimeSeconds);
    if (EngineRemote::Is(fastHash))
    {
        return PlaybackEndDetector::ToString(impl->remoteCalls.Classify(fastHash, end));
    }
    return NoteIfEnded(impl->session, fastHash, PlaybackEndDetector::GetInstance().Classify(fastHash, end));
}

std::string Engine::ClassifyLocalPlaybackEnd(
    const std::string& fastHash, double positionSeconds, const std::string& endReason) const
{
    const auto end = PlaybackEndDetector::FromLocalEnd(endReason, positionSeconds);
    if (EngineRemote::Is(fastHash))
    {
        return PlaybackEndDetector::ToString(impl->remoteCalls.Classify(fastHash, end));
    }
    return NoteIfEnded(impl->session, fastHash, PlaybackEndDetector::GetInstance().Classify(fastHash, end));
}

void Engine::NotifyPlaybackStoppedByUser(const std::string& fastHash)
{
    Log::Info(LogCategory::PlaybackEnd, "stopped by the user: {}", fastHash);
    if (EngineRemote::Is(fastHash))
    {
        impl->remoteCalls.NoteStoppedByUser(fastHash);
        return;
    }
    PlaybackEndDetector::GetInstance().NotifyStoppedByUser(fastHash);
    NoteIfEnded(impl->session, fastHash, PlaybackEndKind::Stopped);
}



bool Engine::GetAutoplayNextInFolder() const { return AutoplaySetting::Get(); }

void Engine::SetAutoplayNextInFolder(bool enabled) { AutoplaySetting::Set(enabled); }

bool Engine::GetListenOnlyWhileStreaming() const { return ListenModeSetting::GetListenOnlyWhileStreaming(); }

void Engine::SetListenOnlyWhileStreaming(bool enabled)
{
    ListenModeSetting::SetListenOnlyWhileStreaming(enabled);
    impl->session.SetListenOnlyWhileStreaming(enabled);
}

std::string Engine::GetSettingsFolder() const { return File::ToUtf8(File::getApplicationDataPath()); }

std::string Engine::GetLogsFolder() const
{
    return File::ToUtf8(File::getApplicationDataPath() / Settings::GetStringSetting("streamsFolder").value_or("streams"));
}

void Engine::Log(const std::string& level, const std::string& category, const std::string& message)
{
    // An unknown level is still worth keeping: the message is what matters.
    ::Log::Write(::Log::LevelFromString(level).value_or(LogLevel::Info), category, message);
}

void Engine::SetAppVersion(const std::string& version)
{
    ::Log::SetAppVersion(version);
    impl->remote.SetAppVersion(version);
}

Session& Engine::GetSession() { return *impl->appSession; }

RemoteStreaming& Engine::GetRemote() { return impl->remote; }

const RemoteStreaming& Engine::GetRemote() const { return impl->remote; }

}  // namespace mango
