#include "SessionCore.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <glaze/glaze.hpp>

#include "log/Log.hpp"

namespace mango
{

namespace
{

constexpr auto kSortOrderKey = "librarySortOrder";
constexpr auto kLastUsedDeviceKey = "lastUsedDevice";
constexpr auto kAppPreferencesImportedKey = "appPreferencesImported";
constexpr auto kAirPlayId = "airplay";

std::string DeviceId(const CastDevice& device) { return std::format("{}:{}", device.ipAddress, device.port); }

std::int64_t UnixMs(std::chrono::system_clock::time_point at)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(at.time_since_epoch()).count();
}

template <typename T>
std::string ToJson(const T& value)
{
    return glz::write_json(value).value_or("{}");
}

}  // namespace

SessionCore::SessionCore(std::unique_ptr<SessionBackend> backend, SessionTimings timings)
    : m_backend(std::move(backend)),
      m_timings(timings),
      m_notifier(m_thread, timings.notifyInterval),
      m_starter(*m_backend, m_thread, m_streamWorker, timings.streamReadyPollInterval,
          StreamStarterCallbacks{
              .onSwitchedInPlace = [this](const StreamRequest& request,
                                       const StreamInitiation& result) { OnSwitchedInPlace(request, result); },
              .onReady = [this](const StreamRequest& request,
                             const StreamInitiation& result) { OnStreamReady(request, result); },
              .onFailed = [this](const StreamRequest& request,
                              const std::string& error) { OnStreamFailed(request, error); },
              .onStateChanged = [this] { PublishNowPlaying(); },
          }),
      m_tracker(
          *m_backend, m_thread, m_statusWorker, timings.castPollInterval, [this] { return MakePollContext(); },
          [this](const PolledStatus& polled) { OnPolledStatus(polled); }),
      m_notices(timings.noticeRepeatWindow)
{
    // Every part has a snapshot before the first change.
    PublishAll();
}

SessionCore::~SessionCore() { Shutdown(); }

void SessionCore::Shutdown()
{
    if (m_stopped)
    {
        return;
    }
    m_stopped = true;

    // Workers first: their jobs post back to the thread.
    m_libraryWorker.Stop();
    m_discoveryWorker.Stop();
    m_statusWorker.Stop();
    m_commandWorker.Stop();
    m_streamWorker.Stop();
    m_thread.Stop();

    // Only a started session set them: another engine's session (the
    // macOS AppDelegate has one) must not clear this one's.
    if (m_started)
    {
        m_backend->SetChangeListeners({}, {});
        m_backend->SetFileResetListener({});
        m_backend->SetRemoteNoticeListener({});
    }
}

void SessionCore::Post(SessionThread::Task task) { m_thread.Post(std::move(task)); }

void SessionCore::SetChangedHandler(SessionChangedHandler handler) { m_notifier.SetHandler(std::move(handler)); }

std::string SessionCore::GetPartJson(SessionPart part) const { return m_snapshots.Get(part); }

// ---- Setup and housekeeping ------------------------------------------------

void SessionCore::Start()
{
    if (m_started)
    {
        return;
    }
    m_started = true;
    Log::Info(LogCategory::App, "session starting");

    m_backend->SetChangeListeners([this] { OnLibraryChanged(); }, [this] { Post([this] { RefreshSettings(); }); });
    m_backend->SetFileResetListener([this](const std::string& file, const std::string& backup)
        { Post([this, file, backup] { OnFileReset(file, backup); }); });
    m_backend->SetRemoteNoticeListener([this](const std::string& code, const NoticeParams& params)
        { Post([this, code, params] { OnRemoteNotice(code, params); }); });

    m_lastUsedDeviceId = m_backend->ReadSessionSetting(kLastUsedDeviceKey).value_or("");
    RefreshSettings();
    RefreshLibrary();
    RefreshServer();
    RefreshTranscode();
    RefreshDevices();
    ScheduleHousekeeping();
    // After the rest: it waits for the ffmpeg search (well under a second).
    Post([this] { CheckMediaTools(); });
}

void SessionCore::CheckMediaTools()
{
    if (m_backend->MediaToolsMissing())
    {
        ShowNotice(NoticeCode::FfmpegMissing, {});
    }
}

void SessionCore::ImportAppPreferences(const std::string& lastUsedDeviceId, const std::string& librarySortOrder)
{
    if (m_backend->ReadSessionSetting(kAppPreferencesImportedKey) == "yes")
    {
        return;
    }

    Log::Info(LogCategory::App, "importing the app's preferences: device {}, sort {}",
        lastUsedDeviceId.empty() ? "none" : lastUsedDeviceId, librarySortOrder.empty() ? "none" : librarySortOrder);
    if (!lastUsedDeviceId.empty())
    {
        m_backend->WriteSessionSetting(kLastUsedDeviceKey, lastUsedDeviceId);
        m_lastUsedDeviceId = lastUsedDeviceId;
    }
    if (LibrarySections::SortOrderFromString(librarySortOrder))
    {
        m_backend->WriteSessionSetting(kSortOrderKey, librarySortOrder);
    }
    m_backend->WriteSessionSetting(kAppPreferencesImportedKey, "yes");

    RefreshSettings();
    KeepOrPickSelection();
}

void SessionCore::ScheduleHousekeeping()
{
    m_housekeepingTimer = m_thread.PostAfter(m_timings.housekeepingInterval,
        [this]
        {
            Housekeeping();
            ScheduleHousekeeping();
        });
}

void SessionCore::Housekeeping()
{
    RefreshServer();
    RefreshTranscode();
    RefreshSleep();
}

void SessionCore::RefreshServer()
{
    m_server = m_backend->GetStatus();
    PublishServer();
}

void SessionCore::RefreshTranscode()
{
    const auto previousPercent = m_transcode.percent;
    m_transcode = m_backend->GetTranscodeStatus();

    // Moving, not merely below 100: a conversion whose ffmpeg died stays
    // short of 100 and must not keep the computer awake forever.
    if (m_transcode.percent >= 0 && m_transcode.percent != previousPercent)
    {
        m_sleep.Note(SleepActivity::Clock::now());
    }
    PublishTranscode();
}

void SessionCore::RefreshSleep()
{
    const auto now = SleepActivity::Clock::now();

    // The gap between two episodes fetches nothing: keep awake through the
    // countdown and the next stream's start too.
    if (m_autoplay.Countdown() || m_starter.IsStarting())
    {
        m_sleep.Note(now);
    }

    // A playing client keeps fetching segments, whoever started it.
    if (const auto seconds = m_backend->SecondsSinceLastStreamRequest(); seconds >= 0)
    {
        m_sleep.Note(now - std::chrono::duration_cast<SleepActivity::Clock::duration>(
                               std::chrono::duration<double>(seconds)));
    }

    m_preventSleep = m_sleep.ShouldPrevent(now);
    PublishNowPlaying();
}

// Any thread, possibly with the library's lock held: only schedules.
void SessionCore::OnLibraryChanged()
{
    if (!m_libraryRefreshPending.exchange(true))
    {
        Post([this] { ScheduleLibraryRefresh(); });
    }
}

void SessionCore::ScheduleLibraryRefresh()
{
    const auto at = std::max(SessionThread::Clock::now(), m_lastLibraryRefresh + m_timings.libraryRefreshInterval);
    m_thread.PostAt(at,
        [this]
        {
            m_libraryRefreshPending = false;
            RefreshLibrary();
        });
}

void SessionCore::RefreshLibrary()
{
    m_lastLibraryRefresh = SessionThread::Clock::now();
    m_library.entries = m_backend->GetLibraryEntries();
    m_library.folders = m_backend->GetLibraryFolders();
    m_library.scan = m_backend->GetScanStatus();

    m_entryIndex.clear();
    for (std::size_t i = 0; i < m_library.entries.size(); ++i)
    {
        m_entryIndex.emplace(m_library.entries[i].fastHash, i);
    }

    RebuildSections();
}

void SessionCore::RebuildSections()
{
    m_library.sections = LibrarySections::Build(m_library.entries, m_library.folders, m_sortOrder);
    PublishLibrary();
    PublishAutoplay();
    PublishNowPlaying();
}

void SessionCore::RefreshSettings()
{
    m_settings = m_backend->ReadSettings();

    const auto order = LibrarySections::SortOrderFromString(m_settings.librarySortOrder)
                           .value_or(LibrarySections::SortOrder::DateAdded);
    m_settings.librarySortOrder = LibrarySections::ToString(order);
    if (order != m_sortOrder)
    {
        m_sortOrder = order;
        RebuildSections();
    }
    PublishSettings();
    PublishAutoplay();
}

// The cards' pre-selected tracks follow the preferences.
void SessionCore::RefreshPreferences()
{
    RefreshSettings();
    RefreshLibrary();
}

// ---- Library and settings commands -----------------------------------------

void SessionCore::AddFile(const std::string& path)
{
    // Probes the file: off the session thread.
    m_libraryWorker.Run(
        [this, path]
        {
            if (!m_backend->AddFile(path))
            {
                Post([this, path] { ShowNotice(NoticeCode::FileNotAdded, {{"path", path}}); });
            }
        });
}

void SessionCore::AddFolder(const std::string& path) { m_backend->AddFolder(path); }

void SessionCore::SelectFile() { m_backend->SelectFile(); }

void SessionCore::SelectFolder() { m_backend->SelectFolder(); }

void SessionCore::RescanLibraryFolders() { m_backend->RescanLibraryFolders(); }

void SessionCore::SetSortOrder(const std::string& sortOrder)
{
    if (!LibrarySections::SortOrderFromString(sortOrder))
    {
        Log::Warning(LogCategory::UI, "unknown sort order {}", sortOrder);
        return;
    }

    Log::Info(LogCategory::UI, "sort {} → {}", m_settings.librarySortOrder, sortOrder);
    m_backend->WriteSessionSetting(kSortOrderKey, sortOrder);
    RefreshSettings();
}

void SessionCore::SetVideoTranscodeMode(const std::string& mode)
{
    m_backend->SetVideoTranscodeMode(mode);
    RefreshSettings();
}

void SessionCore::SetEncodingPreset(const std::string& preset)
{
    m_backend->SetEncodingPreset(preset);
    RefreshSettings();
}

void SessionCore::SetAutoplayNextInFolder(bool enabled)
{
    // Applies from the next video's end; a countdown already running is
    // left alone — Cancel is right there.
    m_backend->SetAutoplayNextInFolder(enabled);
    RefreshSettings();
}

void SessionCore::SetListenOnlyWhileStreaming(bool enabled)
{
    m_backend->SetListenOnlyWhileStreaming(enabled);
    RefreshSettings();
    RefreshServer();
}

void SessionCore::SetLogDebug(bool enabled)
{
    m_backend->SetLogDebug(enabled);
    RefreshSettings();
}

void SessionCore::SetShowSystemNotifications(bool enabled)
{
    m_backend->SetShowSystemNotifications(enabled);
    RefreshSettings();
}

void SessionCore::SetAudioLanguages(const std::vector<std::string>& codes)
{
    m_backend->SetAudioLanguages(codes);
    RefreshPreferences();
}

void SessionCore::SetSubtitleMode(SubtitleMode mode)
{
    m_backend->SetSubtitleMode(mode);
    RefreshPreferences();
}

void SessionCore::SetSubtitleLanguages(const std::vector<std::string>& codes)
{
    m_backend->SetSubtitleLanguages(codes);
    RefreshPreferences();
}

void SessionCore::ForgetFolderTracks()
{
    Log::Info(LogCategory::UI, "forget folder tracks");
    m_backend->ForgetFolderTracks();
    RefreshLibrary();
}

// ---- Devices ---------------------------------------------------------------

void SessionCore::RefreshDevices()
{
    if (m_discovering)
    {
        return;
    }
    Log::Info(LogCategory::UI, "rescan devices");

    m_discovering = true;
    PublishDevices();
    m_discoveryWorker.Run(
        [this]
        {
            const auto result = m_backend->DiscoverDevices();
            Post([this, result] { ApplyDevices(result); });
        });
}

void SessionCore::ApplyDevices(const CastDiscoveryResult& result)
{
    Log::Info(LogCategory::UI, "devices {} → {}{}", m_castDevices.size(), result.devices.size(),
        result.succeeded ? "" : " (discovery failed)");

    m_castDevices = result.devices;
    m_discovering = false;
    m_discoveredOnce = true;
    // A condition shown from the devices part, not a notice.
    m_discoveryFailed = !result.succeeded;

    KeepOrPickSelection();
    PublishDevices();
}

// Keeps what's selected while it's still there (AirPlay never drops off);
// otherwise the last used device if it's in the list, else the first
// Chromecast, else AirPlay — something is selected whenever anything can be.
void SessionCore::KeepOrPickSelection()
{
    // Before the first discovery only AirPlay could be picked, and picking
    // it would replace the remembered Chromecast.
    if (!m_discoveredOnce)
    {
        return;
    }

    const auto receivers = Receivers();
    const auto isListed = [&receivers](const std::string& receiverId)
    { return std::ranges::contains(receivers, receiverId, &SessionDevice::receiverId); };

    if (!m_selectedReceiverId.empty() && isListed(m_selectedReceiverId))
    {
        return;
    }

    std::string pick;
    if (!m_lastUsedDeviceId.empty() && isListed(m_lastUsedDeviceId))
    {
        pick = m_lastUsedDeviceId;
    }
    else if (!receivers.empty())
    {
        pick = receivers.front().receiverId;
    }
    ApplySelection(pick);
}

void SessionCore::SelectDevice(const std::string& receiverId)
{
    if (!receiverId.empty() && !std::ranges::contains(Receivers(), receiverId, &SessionDevice::receiverId))
    {
        Log::Warning(LogCategory::UI, "device {} not found", receiverId);
        return;
    }
    ApplySelection(receiverId);
}

void SessionCore::SetAirPlayAvailable(bool available)
{
    m_airPlayAvailable = available;
    if (!available && IsAirPlaySelected())
    {
        m_selectedReceiverId.clear();
    }
    KeepOrPickSelection();
    PublishDevices();
}

void SessionCore::ApplySelection(const std::string& receiverId)
{
    if (receiverId == m_selectedReceiverId)
    {
        return;
    }

    Log::Info(LogCategory::UI, "device {} → {}", m_selectedReceiverId.empty() ? "none" : m_selectedReceiverId,
        receiverId.empty() ? "none" : receiverId);
    m_selectedReceiverId = receiverId;

    // Remembered so the next discovery (or launch) picks it again.
    if (!receiverId.empty())
    {
        m_lastUsedDeviceId = receiverId;
        m_backend->WriteSessionSetting(kLastUsedDeviceKey, receiverId);
    }

    m_tracker.SetDevice(SelectedCastDevice());
    PublishDevices();
    PublishNowPlaying();
}

std::vector<SessionDevice> SessionCore::Receivers() const
{
    std::vector<SessionDevice> receivers;
    for (const auto& device : m_castDevices)
    {
        receivers.push_back(SessionDevice{DeviceId(device), "chromecast", device.name, device.ipAddress, device.port});
    }
    if (m_airPlayAvailable)
    {
        receivers.push_back(SessionDevice{kAirPlayId, "airplay", "AirPlay", "", 0});
    }
    return receivers;
}

std::optional<CastDevice> SessionCore::SelectedCastDevice() const
{
    const auto it = std::ranges::find_if(
        m_castDevices, [this](const CastDevice& device) { return DeviceId(device) == m_selectedReceiverId; });
    return it == m_castDevices.end() ? std::nullopt : std::optional(*it);
}

bool SessionCore::IsAirPlaySelected() const { return m_selectedReceiverId == kAirPlayId; }

// ---- Lookups ---------------------------------------------------------------

const LibraryEntry* SessionCore::FindEntry(const std::string& fastHash) const
{
    const auto it = m_entryIndex.find(fastHash);
    return it == m_entryIndex.end() ? nullptr : &m_library.entries[it->second];
}

std::optional<std::string> SessionCore::NextFastHash(const std::string& fastHash) const
{
    if (m_autoplay.SectionId().empty())
    {
        return std::nullopt;
    }
    return LibrarySections::NextFastHash(m_library.sections, m_autoplay.SectionId(), fastHash);
}

std::string SessionCore::NoNextReason() const
{
    return m_autoplay.SectionId().empty() ? "noSection" : "lastInSection";
}

// ---- Publishing ------------------------------------------------------------

void SessionCore::PublishAll()
{
    PublishServer();
    PublishLibrary();
    PublishTranscode();
    PublishDevices();
    PublishNowPlaying();
    PublishAutoplay();
    PublishSettings();
    PublishNotices();
}

void SessionCore::PublishServer()
{
    if (m_snapshots.Store(SessionPart::Server, ToJson(m_server)))
    {
        m_notifier.Mark(SessionPart::Server);
    }
}

void SessionCore::PublishLibrary()
{
    m_library.trackChoices = BuildTrackChoices();
    if (m_snapshots.Store(SessionPart::Library, ToJson(m_library)))
    {
        m_notifier.Mark(SessionPart::Library);
    }
}

void SessionCore::PublishTranscode()
{
    if (m_snapshots.Store(SessionPart::Transcode, ToJson(m_transcode)))
    {
        m_notifier.Mark(SessionPart::Transcode);
    }
}

void SessionCore::PublishDevices()
{
    const DevicesState state{Receivers(), m_discovering, m_discoveryFailed, m_selectedReceiverId};
    if (m_snapshots.Store(SessionPart::Devices, ToJson(state)))
    {
        m_notifier.Mark(SessionPart::Devices);
    }
}

void SessionCore::PublishNowPlaying()
{
    if (m_snapshots.Store(SessionPart::NowPlaying, ToJson(BuildNowPlaying())))
    {
        m_notifier.Mark(SessionPart::NowPlaying);
    }
}

void SessionCore::PublishAutoplay()
{
    if (m_snapshots.Store(SessionPart::Autoplay, ToJson(BuildAutoplay())))
    {
        m_notifier.Mark(SessionPart::Autoplay);
    }
}

void SessionCore::PublishSettings()
{
    if (m_snapshots.Store(SessionPart::Settings, ToJson(m_settings)))
    {
        m_notifier.Mark(SessionPart::Settings);
    }
}

void SessionCore::PublishNotices()
{
    if (m_snapshots.Store(SessionPart::Notice, ToJson(m_notices.State())))
    {
        m_notifier.Mark(SessionPart::Notice);
    }
}

// ---- Notices ---------------------------------------------------------------

void SessionCore::ShowNotice(std::string_view code, NoticeParams params, const std::string& fastHash)
{
    std::string described;
    for (const auto& [name, value] : params)
    {
        described += std::format(" {}={}", name, value);
    }
    const auto* notice = m_notices.Add(code, std::move(params), fastHash, NoticeList::Clock::now());
    if (notice == nullptr)
    {
        Log::Debug(LogCategory::App, "notice {}{} not shown again", code, described);
        return;
    }

    Log::Info(LogCategory::App, "notice {} ({}){}{}", code, notice->severity, described,
        fastHash.empty() ? "" : " (" + fastHash + ")");
    PublishNotices();
}

void SessionCore::OnFileReset(const std::string& file, const std::string& backup)
{
    ShowNotice(NoticeCode::SettingsFileReset, {{"file", file}, {"backup", backup}});
}

void SessionCore::OnRemoteNotice(const std::string& code, NoticeParams params)
{
    if (Notices::Find(code) == nullptr)
    {
        Log::Warning(LogCategory::App, "remote notice {} isn't in the catalog", code);
        return;
    }
    ShowNotice(code, std::move(params));
}

NoticeParams SessionCore::TitleParams(const std::string& fastHash) const
{
    if (const auto* entry = FindEntry(fastHash))
    {
        return {{"title", entry->title}};
    }
    return {};
}

std::string SessionCore::SelectedDeviceName() const
{
    const auto receivers = Receivers();
    const auto it = std::ranges::find(receivers, m_selectedReceiverId, &SessionDevice::receiverId);
    return it == receivers.end() ? std::string() : it->name;
}

std::string SessionCore::PlaybackStateName() const
{
    if (m_starter.IsStarting())
    {
        return "starting";
    }
    if (m_loading)
    {
        return "loading";
    }
    if (const auto& status = m_tracker.Status())
    {
        std::string state = status->state;
        std::ranges::transform(state, state.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return state == "active" ? "idle" : state;
    }
    return m_local.url.empty() ? "none" : "local";
}

NowPlayingState SessionCore::BuildNowPlaying() const
{
    NowPlayingState state;
    state.state = PlaybackStateName();
    state.target = m_playing.target;
    state.deviceId = m_selectedReceiverId;
    state.fastHash = m_playing.fastHash;
    state.audioTrackId = m_playing.audioTrackId;
    state.subtitleTrackId = m_playing.subtitleTrackId;
    state.commandInFlight = m_commandInFlight;
    state.shouldPreventSleep = m_preventSleep;

    if (const auto* entry = FindEntry(m_playing.fastHash))
    {
        state.title = entry->title;
    }
    if (const auto& status = m_tracker.Status())
    {
        state.tvState = status->state;
        state.tvMediaTitle = status->mediaTitle;
        state.tvAppDisplayName = status->appDisplayName;
        state.positionSeconds = status->currentTimeSeconds;
        state.durationSeconds = status->durationSeconds;
        state.isFromThisServer = status->isFromThisServer;
    }
    else if (!m_local.url.empty())
    {
        state.positionSeconds = m_local.positionSeconds;
        if (const auto* entry = FindEntry(m_playing.fastHash))
        {
            state.durationSeconds = entry->durationSeconds;
        }
    }
    if (const auto& wait = m_starter.StartWait())
    {
        state.startWaitBeginUnixMs = UnixMs(wait->first);
        state.startWaitEndUnixMs = UnixMs(wait->second);
    }

    state.localStreamUrl = m_local.url;
    state.localStartSeconds = m_local.startSeconds;
    state.localPlaybackId = m_local.playbackId;
    return state;
}

AutoplayState SessionCore::BuildAutoplay() const
{
    AutoplayState state;
    state.sectionId = m_autoplay.SectionId();

    if (const auto& countdown = m_autoplay.Countdown())
    {
        state.nextFastHash = countdown->nextFastHash;
        state.nextTitle = countdown->nextTitle;
        state.countdownActive = true;
        state.countdownDeadlineUnixMs = UnixMs(countdown->deadline);
        return state;
    }

    const auto& current = m_autoplay.CurrentFastHash();
    if (current.empty())
    {
        return state;
    }
    if (const auto next = NextFastHash(current); next && FindEntry(*next))
    {
        state.nextFastHash = *next;
        state.nextTitle = FindEntry(*next)->title;
    }
    else
    {
        state.noNextReason = NoNextReason();
    }
    return state;
}

}  // namespace mango
