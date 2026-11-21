#include "EngineSessionBackend.hpp"

#include <glaze/glaze.hpp>

#include "configuration/Settings.hpp"
#include "configuration/TrackPreferencesStore.hpp"
#include "file/File.hpp"
#include "media/MediaBackend.hpp"
#include "pipeline/playback/FolderTrackMemory.hpp"
#include "pipeline/library/Library.hpp"
#include "pipeline/playback/PlaybackHistory.hpp"
#include "remote/RemoteStreaming.hpp"
#include "pipeline/stream/profiles/ProfileFiles.hpp"

namespace mango
{

namespace
{

constexpr auto kLogDebugKey = "logDebug";
constexpr auto kShowSystemNotificationsKey = "showSystemNotifications";

// Engine's JSON into T; a default T when it doesn't parse.
template <typename T>
T Parse(const std::string& json, T fallback = {})
{
    constexpr glz::opts lenient{.error_on_unknown_keys = false};
    T value{};
    return glz::read<lenient>(value, json) ? fallback : value;
}

// A remote notice's fields as the catalog's params (see NoticeCode); empty
// ones are left out, items are joined with ", ".
NoticeParams RemoteNoticeParams(const RemoteNotice& notice)
{
    std::string items;
    for (const auto& item : notice.items)
    {
        items += (items.empty() ? "" : ", ") + item;
    }

    NoticeParams params;
    for (const auto& [name, value] : {std::pair<const char*, const std::string&>{"peer", notice.peer},
             {"reason", notice.reason}, {"file", notice.file}, {"items", items}})
    {
        if (!value.empty())
        {
            params.emplace(name, value);
        }
    }
    return params;
}

}  // namespace

EngineSessionBackend::EngineSessionBackend(Engine& engine) : m_engine(engine) {}

EngineSessionBackend::~EngineSessionBackend() = default;

ServerStatus EngineSessionBackend::GetStatus() { return m_engine.GetStatus(); }

// This device's library, then the paired devices' (see
// RemoteStreaming::GetLibraryEntriesJson).
std::vector<LibraryEntry> EngineSessionBackend::GetLibraryEntries()
{
    auto entries = Parse<std::vector<LibraryEntry>>(m_engine.GetLibraryEntriesJson());
    auto remote = Parse<std::vector<LibraryEntry>>(m_engine.GetRemote().GetLibraryEntriesJson());
    entries.insert(entries.end(), std::make_move_iterator(remote.begin()), std::make_move_iterator(remote.end()));
    return entries;
}

std::vector<LibraryFolderEntry> EngineSessionBackend::GetLibraryFolders()
{
    return Parse<std::vector<LibraryFolderEntry>>(m_engine.GetLibraryFoldersJson());
}

LibraryScanInfo EngineSessionBackend::GetScanStatus()
{
    return Parse<LibraryScanInfo>(m_engine.GetLibraryScanStatusJson());
}

TranscodeStatus EngineSessionBackend::GetTranscodeStatus()
{
    return Parse<TranscodeStatus>(m_engine.GetTranscodeStatusJson());
}

double EngineSessionBackend::SecondsSinceLastStreamRequest() { return m_engine.GetSecondsSinceLastStreamRequest(); }

void EngineSessionBackend::SelectFile() { m_engine.SelectFile(); }

void EngineSessionBackend::SelectFolder() { m_engine.SelectFolder(); }

bool EngineSessionBackend::AddFile(const std::string& path) { return m_engine.AddFile(path); }

void EngineSessionBackend::AddFolder(const std::string& path) { m_engine.AddFolder(path); }

void EngineSessionBackend::RescanLibraryFolders() { m_engine.RescanLibraryFolders(); }

SettingsState EngineSessionBackend::ReadSettings()
{
    const auto preferences = TrackPreferencesStore::Get();
    return SettingsState{
        .videoTranscodeMode = m_engine.GetVideoTranscodeMode(),
        .encodingPreset = m_engine.GetEncodingPreset(),
        .autoplayNextInFolder = m_engine.GetAutoplayNextInFolder(),
        .listenOnlyWhileStreaming = m_engine.GetListenOnlyWhileStreaming(),
        .librarySortOrder = ReadSessionSetting("librarySortOrder").value_or("dateAdded"),
        .logDebug = Settings::GetBoolSetting(kLogDebugKey).value_or(false),
        .showSystemNotifications = Settings::GetBoolSetting(kShowSystemNotificationsKey).value_or(true),
        .audioLanguages = preferences.audioLanguages,
        .subtitleMode = std::string(SubtitleModes::ToString(preferences.subtitleMode)),
        .subtitleLanguages = preferences.subtitleLanguages,
    };
}

std::optional<std::string> EngineSessionBackend::ReadSessionSetting(const std::string& key)
{
    return Settings::GetStringSetting(key);
}

void EngineSessionBackend::WriteSessionSetting(const std::string& key, const std::string& value)
{
    Settings::SetStringSetting(key, value);
}

void EngineSessionBackend::SetVideoTranscodeMode(const std::string& mode) { m_engine.SetVideoTranscodeMode(mode); }

void EngineSessionBackend::SetEncodingPreset(const std::string& preset) { m_engine.SetEncodingPreset(preset); }

void EngineSessionBackend::SetAutoplayNextInFolder(bool enabled) { m_engine.SetAutoplayNextInFolder(enabled); }

void EngineSessionBackend::SetListenOnlyWhileStreaming(bool enabled) { m_engine.SetListenOnlyWhileStreaming(enabled); }

void EngineSessionBackend::SetLogDebug(bool enabled) { Settings::SetBoolSetting(kLogDebugKey, enabled); }

void EngineSessionBackend::SetShowSystemNotifications(bool enabled)
{
    Settings::SetBoolSetting(kShowSystemNotificationsKey, enabled);
}

void EngineSessionBackend::SetAudioLanguages(const std::vector<std::string>& codes)
{
    TrackPreferencesStore::SetAudioLanguages(codes);
}

void EngineSessionBackend::SetSubtitleMode(SubtitleMode mode) { TrackPreferencesStore::SetSubtitleMode(mode); }

void EngineSessionBackend::SetSubtitleLanguages(const std::vector<std::string>& codes)
{
    TrackPreferencesStore::SetSubtitleLanguages(codes);
}

void EngineSessionBackend::ForgetFolderTracks() { FolderTrackMemory::GetInstance().Clear(); }

void EngineSessionBackend::SetChangeListeners(std::function<void()> onLibrary, std::function<void()> onSettings)
{
    Library::GetInstance().SetChangeListener(onLibrary);
    PlaybackHistory::GetInstance().SetChangeListener(onLibrary);
    FolderTrackMemory::GetInstance().SetChangeListener(onLibrary);
    m_engine.GetRemote().SetLibraryChangedHandler(onLibrary);
    Settings::SetChangeListener(std::move(onSettings));
}

void EngineSessionBackend::SetFileResetListener(FileResetListener listener)
{
    if (!listener)
    {
        ProfileFiles::SetQuarantineListener({});
        return;
    }
    ProfileFiles::SetQuarantineListener(
        [listener = std::move(listener)](const std::filesystem::path& file, const std::filesystem::path& backup)
        { listener(File::ToUtf8(file.filename()), File::ToUtf8(backup.filename())); });
}

void EngineSessionBackend::SetRemoteNoticeListener(RemoteNoticeListener listener)
{
    if (!listener)
    {
        m_engine.GetRemote().SetNoticeHandler({});
        return;
    }
    m_engine.GetRemote().SetNoticeHandler([listener = std::move(listener)](const RemoteNotice& notice)
        { listener(notice.code, RemoteNoticeParams(notice)); });
}

bool EngineSessionBackend::MediaToolsMissing() { return !MediaBackends::Get().HasTools(); }

StreamInitiation EngineSessionBackend::InitiateStream(
    const std::string& fastHash, const TrackRequest& tracks, PlaylistType playlist, const std::string& deviceKey)
{
    StreamInitiation failed;
    failed.error = "unknown";
    return Parse<StreamInitiation>(
        m_engine.InitiateStreamJson(fastHash, tracks, playlist, deviceKey), failed);
}

int EngineSessionBackend::StreamStartDelayMs() { return m_engine.GetStreamStartDelayMs(); }

bool EngineSessionBackend::IsStreamStartReady() { return m_engine.IsStreamStartReady(); }

void EngineSessionBackend::ReportPlaybackProgress(
    const std::string& fastHash, double positionSeconds, int audioTrackId, int subtitleTrackId)
{
    m_engine.ReportPlaybackProgress(fastHash, positionSeconds, audioTrackId, subtitleTrackId);
}

std::string EngineSessionBackend::ClassifyCastEnd(const std::string& fastHash, const CastPlaybackStatus& status)
{
    return m_engine.ClassifyCastPlaybackEnd(fastHash, glz::write_json(status).value_or("{}"));
}

std::string EngineSessionBackend::ClassifyLocalEnd(
    const std::string& fastHash, double positionSeconds, const std::string& endReason)
{
    return m_engine.ClassifyLocalPlaybackEnd(fastHash, positionSeconds, endReason);
}

void EngineSessionBackend::NotifyStoppedByUser(const std::string& fastHash)
{
    m_engine.NotifyPlaybackStoppedByUser(fastHash);
}

CastDiscoveryResult EngineSessionBackend::DiscoverDevices()
{
    CastDiscoveryResult failed;
    failed.succeeded = false;
    return Parse<CastDiscoveryResult>(m_engine.DiscoverCastDevicesJson(), failed);
}

CastPlaybackStatus EngineSessionBackend::QueryStatus(const CastDevice& device)
{
    CastPlaybackStatus unreachable;
    unreachable.state = "Unreachable";
    return Parse<CastPlaybackStatus>(m_engine.QueryCastPlaybackStatusJson(device.ipAddress, device.port), unreachable);
}

std::string EngineSessionBackend::Play(const CastDevice& device)
{
    return m_engine.PlayCastDevice(device.ipAddress, device.port);
}

std::string EngineSessionBackend::Pause(const CastDevice& device)
{
    return m_engine.PauseCastDevice(device.ipAddress, device.port);
}

std::string EngineSessionBackend::Stop(const CastDevice& device)
{
    return m_engine.StopCastDevice(device.ipAddress, device.port);
}

std::string EngineSessionBackend::Seek(const CastDevice& device, double positionSeconds)
{
    return m_engine.SeekCastDevice(device.ipAddress, device.port, positionSeconds);
}

std::string EngineSessionBackend::Load(const CastDevice& device, const CastLoadRequest& request)
{
    return m_engine.LoadCastDevice(device.ipAddress, device.port, request.contentUrl, request.contentType,
        request.title, request.fastHash, request.subtitleTracksJson, request.activeSubtitleTrackIndex,
        request.startTimeSeconds);
}

std::string EngineSessionBackend::SetSubtitleTrack(const CastDevice& device, const std::string& fastHash, int index)
{
    return m_engine.SetCastSubtitleTrack(device.ipAddress, device.port, fastHash, index);
}

}  // namespace mango
