// mango::Session against FakeSessionBackend: the flows moved from the apps
// (see docs/scope/shared-app-session-scope.md, Testing), plus its pure
// parts — sections, autoplay bookkeeping, track carry-over, coalescing.

#include <atomic>
#include <chrono>
#include <glaze/glaze.hpp>
#include <thread>

#include "FakeSessionBackend.hpp"
#include "TestCheck.hpp"
#include "session/ChangeNotifier.hpp"
#include "session/FolderAutoplay.hpp"
#include "session/LibrarySections.hpp"
#include "session/NoticeList.hpp"
#include "session/Notices.hpp"
#include "session/Session.hpp"

using namespace mango;
using namespace std::chrono_literals;

namespace
{

constexpr auto kFolderSection = "folder:f1/";
constexpr auto kDevice = "10.0.0.5:8009";

bool WaitUntil(const std::function<bool()>& condition, std::chrono::milliseconds timeout = 3s)
{
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < end)
    {
        if (condition())
        {
            return true;
        }
        std::this_thread::sleep_for(5ms);
    }
    return condition();
}

template <typename T>
T Part(const Session& session, SessionPart part)
{
    T value{};
    constexpr glz::opts lenient{.error_on_unknown_keys = false};
    (void)glz::read<lenient>(value, session.GetPartJson(part));
    return value;
}

LibraryEntry Entry(const std::string& fastHash, const std::string& title, std::int64_t addedAt,
    const std::string& folderPath = "", bool manual = false)
{
    LibraryEntry entry;
    entry.fastHash = fastHash;
    entry.title = title;
    entry.addedAtUnixSeconds = addedAt;
    entry.addedManually = manual;
    entry.durationSeconds = 6000;
    entry.defaultAudioTrackId = 1;
    entry.subtitleTracks = {{3, "en", "", false, {}}, {4, "ru", "", false, {}}};
    if (!manual)
    {
        entry.folderLocations = {{"f1", folderPath.empty() ? title : folderPath + "/" + title, folderPath}};
    }
    return entry;
}

SessionTimings FastTimings()
{
    return SessionTimings{
        .notifyInterval = 5ms,
        .castPollInterval = 30ms,
        .housekeepingInterval = 30ms,
        .libraryRefreshInterval = 5ms,
        .streamReadyPollInterval = 5ms,
        .autoplayCountdown = 150ms,
    };
}

// A session over a folder "Show" with Episode 1..4 (h1..h4), one TV, all
// started and the TV selected.
struct Fixture
{
    FakeSessionBackend* fake = nullptr;
    std::unique_ptr<Session> session;

    explicit Fixture(bool mediaToolsMissing = false)
    {
        auto backend = std::make_unique<FakeSessionBackend>();
        fake = backend.get();
        fake->mediaToolsMissing = mediaToolsMissing;
        fake->folders = {{"f1", "Show", "/media/Show", 1}};
        for (int i = 1; i <= 4; ++i)
        {
            fake->entries.push_back(Entry("h" + std::to_string(i), "Episode " + std::to_string(i), 100));
        }
        fake->entries[0].resumeStartSeconds = 120;

        session = std::make_unique<Session>(std::move(backend), FastTimings());
        session->Start();
        WaitUntil([this] { return Devices().selectedReceiverId == kDevice && !Library().entries.empty(); });
    }

    DevicesState Devices() const { return Part<DevicesState>(*session, SessionPart::Devices); }
    LibraryState Library() const { return Part<LibraryState>(*session, SessionPart::Library); }
    NowPlayingState NowPlaying() const { return Part<NowPlayingState>(*session, SessionPart::NowPlaying); }
    AutoplayState Autoplay() const { return Part<AutoplayState>(*session, SessionPart::Autoplay); }
    // The newest notice; an empty one before the first.
    SessionNotice Notice() const
    {
        const auto notices = Part<NoticeState>(*session, SessionPart::Notice).notices;
        return notices.empty() ? SessionNotice{} : notices.back();
    }
    std::vector<SessionNotice> Notices() const { return Part<NoticeState>(*session, SessionPart::Notice).notices; }
    SettingsState Settings() const { return Part<SettingsState>(*session, SessionPart::Settings); }

    void SetTv(const std::string& state, const std::string& fastHash, double position, const std::string& idleReason = "")
    {
        auto lock = fake->Lock();
        fake->tvStatus = CastPlaybackStatus{.state = state,
            .currentTimeSeconds = position,
            .durationSeconds = 6000,
            .isFromThisServer = true,
            .fastHash = fastHash,
            .idleReason = idleReason};
    }

    bool HasCall(const std::string& call) const
    {
        return std::ranges::contains(fake->Calls(), call);
    }

    // Plays fastHash from the folder section and waits until the TV is
    // seen playing it.
    void PlayAndWatch(const std::string& fastHash)
    {
        session->Play(fastHash, kFolderSection);
        WaitUntil([&] { return fake->CountCalls("load " + fastHash) > 0; });
        SetTv("Playing", fastHash, 100);
        WaitUntil([&] { return NowPlaying().state == "playing" && NowPlaying().fastHash == fastHash; });
        // One more poll, so the end detector has seen it playing.
        std::this_thread::sleep_for(80ms);
    }
};

// ---- Pure parts ------------------------------------------------------------

void SectionNameOrder()
{
    Check(LibrarySections::CompareNames("Episode 2", "Episode 10") < 0, "digit runs compare by value");
    Check(LibrarySections::CompareNames("episode 1", "Episode 1") == 0, "case doesn't matter");
    Check(LibrarySections::CompareNames("Ep 007", "Ep 7") == 0, "leading zeros don't matter");
    Check(LibrarySections::CompareNames("Abc", "Abcd") < 0, "a prefix comes first");
}

void SectionLayout()
{
    const std::vector<LibraryEntry> entries = {
        Entry("m1", "Manual", 50, "", true),
        Entry("a", "Episode 10", 10),
        Entry("b", "Episode 2", 30),
        Entry("c", "Extra", 20, "Season 1"),
    };
    const std::vector<LibraryFolderEntry> folders = {{"f1", "Show", "/media/Show", 1}};

    const auto byName = LibrarySections::Build(entries, folders, LibrarySections::SortOrder::Name);
    Check(byName.size() == 3, "manual, folder and subfolder sections");
    Check(byName.size() == 3 && byName[0].sectionId == "manual" && byName[1].sectionId == "folder:f1/" &&
              byName[2].sectionId == "folder:f1/Season 1",
        "sections in tree order");
    Check(byName.size() == 3 && byName[1].fastHashes == std::vector<std::string>{"b", "a"}, "videos by name");
    Check(byName.size() == 3 && byName[2].parentSectionId == "folder:f1/" &&
              byName[2].breadcrumb == std::vector<std::string>{"Show", "Season 1"},
        "subfolder breadcrumb and parent");

    const auto byDate = LibrarySections::Build(entries, folders, LibrarySections::SortOrder::DateAdded);
    Check(byDate.size() == 3 && byDate[1].fastHashes == std::vector<std::string>{"b", "a"}, "newest first");
    Check(LibrarySections::NextFastHash(byName, "folder:f1/", "b") == "a", "next in the section");
    Check(!LibrarySections::NextFastHash(byName, "folder:f1/", "a"), "nothing after the last");
}

void AutoplayBookkeeping()
{
    FolderAutoplay autoplay;
    autoplay.Request("h1", "folder:f1/", false);
    autoplay.StreamStarted("h1");
    const int token = autoplay.Token();
    Check(!autoplay.AwaitsEnd("h1", token), "no end before it was seen playing");
    autoplay.NotePlaying("h1", token);
    Check(autoplay.AwaitsEnd("h1", token), "an end after playing counts");
    Check(!autoplay.AwaitsEnd("h1", token - 1), "a status from before the start doesn't");
    autoplay.MarkEndHandled();
    Check(!autoplay.AwaitsEnd("h1", token), "only the first end counts");

    autoplay.Request("h2", "folder:f1/", true);
    Check(autoplay.NoteUnreadable() && autoplay.NoteUnreadable() && !autoplay.NoteUnreadable(),
        "the chain stops after three unreadable files");
    autoplay.Request("h3", "manual", false);
    Check(autoplay.SectionId().empty(), "Added Manually has no playlist");
}

void NotifierCoalesces()
{
    SessionThread thread;
    ChangeNotifier notifier(thread, 80ms);
    std::mutex mutex;
    std::vector<SessionParts> calls;
    notifier.SetHandler(
        [&](SessionParts parts)
        {
            std::lock_guard lock(mutex);
            calls.push_back(parts);
        });

    const auto count = [&]
    {
        std::lock_guard lock(mutex);
        return calls.size();
    };

    notifier.Mark(SessionPart::Library);
    WaitUntil([&] { return count() == 1; });
    notifier.Mark(SessionPart::Devices);
    notifier.Mark(SessionPart::Settings);
    notifier.Mark(SessionPart::Devices);
    std::this_thread::sleep_for(30ms);
    Check(count() == 1, "no second call within the interval");
    WaitUntil([&] { return count() == 2; });

    std::lock_guard lock(mutex);
    Check(calls.size() == 2 && calls[1] == (SessionParts(SessionPart::Devices) | SessionPart::Settings),
        "one call with every part marked since");
    thread.Stop();
}

// ---- Flows -----------------------------------------------------------------

void PlayStartsAndCasts()
{
    Fixture f;
    f.session->Play("h1", kFolderSection);
    Check(WaitUntil([&] { return f.HasCall("load h1 start=120 sub=-1"); }), "cast from the resume point");
    Check(f.HasCall("initiate h1 audio=auto sub=auto"), "no pick: the engine picks both");
    Check(f.NowPlaying().fastHash == "h1" && f.NowPlaying().target == "chromecast", "now playing h1 on the TV");

    Check(f.Library().trackChoices.empty(), "nothing pending: the engine's history has the tracks");
    Check(f.Autoplay().nextFastHash == "h2", "up next is the following episode");
}

void FreshConversionWaitsReusedDoesNot()
{
    Fixture f;
    {
        auto lock = f.fake->Lock();
        f.fake->startedNewTranscode = true;
        f.fake->streamStartDelayMs = 10000;
    }
    f.session->Play("h1", kFolderSection);
    Check(WaitUntil([&] { return f.NowPlaying().startWaitEndUnixMs > 0; }), "a fresh conversion gets a head start");
    Check(f.NowPlaying().state == "starting", "shown as starting");
    std::this_thread::sleep_for(50ms);
    Check(f.fake->CountCalls("load") == 0, "not cast while waiting");

    f.session->Play("h2", kFolderSection);
    std::this_thread::sleep_for(30ms);
    Check(f.fake->CountCalls("initiate h2") == 0, "a Play while one is starting is ignored");

    {
        auto lock = f.fake->Lock();
        f.fake->streamStartReady = true;
    }
    Check(WaitUntil([&] { return f.HasCall("load h1 start=120 sub=-1"); }), "cast once the first segments are ready");

    {
        auto lock = f.fake->Lock();
        f.fake->startedNewTranscode = false;
        f.fake->streamStartReady = false;
    }
    f.session->Play("h2", kFolderSection);
    Check(WaitUntil([&] { return f.fake->CountCalls("load h2") == 1; }, 500ms), "a reused conversion is cast at once");
}

void SubtitleSwitchesInPlace()
{
    Fixture f;
    f.PlayAndWatch("h1");
    f.session->SelectSubtitleTrack(3);
    Check(WaitUntil([&] { return f.HasCall("subtitle h1 3"); }), "switched on the TV");
    Check(WaitUntil([&] { return f.NowPlaying().subtitleTrackId == 3; }), "now playing shows the track");
    Check(f.fake->CountCalls("load h1") == 1, "not reloaded");

    {
        auto lock = f.fake->Lock();
        f.fake->subtitleSwitchResult = "TrackNotAvailable";
    }
    f.SetTv("Playing", "h1", 300);
    std::this_thread::sleep_for(80ms);
    f.session->SelectSubtitleTrack(4);
    Check(WaitUntil([&] { return f.HasCall("load h1 start=300 sub=4"); }), "otherwise reloaded where the TV is");
}

void StartFailureShowsNotice()
{
    Fixture f;
    {
        auto lock = f.fake->Lock();
        f.fake->initiateErrors["h1"] = "noVideoEncoder";
    }
    f.session->Play("h1", kFolderSection);
    Check(WaitUntil([&] { return f.Notice().code == "streamStartFailed"; }), "a notice for the failure");
    const auto notice = f.Notice();
    Check(notice.params.at("error") == "noVideoEncoder" && notice.params.at("title") == "Episode 1" &&
              notice.fastHash == "h1" && notice.noticeId > 0,
        "with the reason and file");
    Check(notice.severity == "error" && !notice.time.empty(), "an error, with its time");
    Check(f.fake->CountCalls("load") == 0, "nothing cast");
    Check(f.fake->CountCalls("initiate h2") == 0, "a Play's failure doesn't skip ahead");
}

void SeekToTheEndReportsFirst()
{
    Fixture f;
    f.PlayAndWatch("h1");
    f.session->Seek(5990);
    Check(WaitUntil([&] { return f.HasCall("report h1 5990"); }), "the seek position is reported");

    const auto calls = f.fake->Calls();
    const auto seek = std::ranges::find(calls, "seek 5990");
    const auto report = std::ranges::find(calls, "report h1 5990");
    Check(seek != calls.end() && report != calls.end() && seek < report, "right after the seek");
}

void FinishedStartsNextAfterCountdown()
{
    Fixture f;
    f.PlayAndWatch("h1");
    f.SetTv("Idle", "", 6000, "FINISHED");
    Check(WaitUntil([&] { return f.Autoplay().countdownActive; }), "the countdown starts");
    Check(f.Autoplay().nextFastHash == "h2", "to the next episode");
    Check(f.Notice().code == "playbackFinished", "the end is a notice");
    Check(f.Notice().systemNotification && f.Notice().params.at("title") == "Episode 1", "for the system too");
    Check(WaitUntil([&] { return f.fake->CountCalls("initiate h2") == 1; }), "the next one starts when it ends");
    Check(f.HasCall("initiate h2 audio=auto sub=auto"), "no picks sent: folder memory carries the season");
}

void CancelAndNow()
{
    Fixture f;
    f.PlayAndWatch("h1");
    f.SetTv("Idle", "", 6000, "FINISHED");
    WaitUntil([&] { return f.Autoplay().countdownActive; });
    f.session->CancelAutoplay();
    Check(WaitUntil([&] { return !f.Autoplay().countdownActive; }), "Cancel stops the countdown");
    std::this_thread::sleep_for(250ms);
    Check(f.fake->CountCalls("initiate h2") == 0, "and the chain");

    f.session->PlayNextNow();
    Check(WaitUntil([&] { return f.fake->CountCalls("initiate h2") == 1; }), "Next starts the following one at once");
}

void UnreadableFilesAreSkipped()
{
    Fixture f;
    {
        auto lock = f.fake->Lock();
        f.fake->entries.push_back(Entry("h5", "Episode 5", 100));
        for (const auto* hash : {"h2", "h3", "h4", "h5"})
        {
            f.fake->initiateErrors[hash] = "fileUnreadable";
        }
    }
    f.fake->NotifyLibraryChanged();
    WaitUntil([&] { return f.Library().entries.size() == 5; });
    f.PlayAndWatch("h1");
    f.SetTv("Idle", "", 6000, "FINISHED");

    Check(WaitUntil([&] { return f.fake->CountCalls("initiate h4") == 1; }), "unreadable files are skipped");
    std::this_thread::sleep_for(100ms);
    Check(f.fake->CountCalls("initiate h2") == 1 && f.fake->CountCalls("initiate h3") == 1, "one try each");
    Check(f.fake->CountCalls("initiate h5") == 0, "the chain stops after three");
}

void OneCastCommandAtATime()
{
    Fixture f;
    f.PlayAndWatch("h1");
    f.fake->HoldCommands(true);
    f.session->PlayPause();
    WaitUntil([&] { return f.NowPlaying().commandInFlight; });
    f.session->PlayPause();
    f.session->Seek(10);
    f.session->Seek(20);
    std::this_thread::sleep_for(50ms);
    f.fake->HoldCommands(false);

    Check(WaitUntil([&] { return f.HasCall("seek 20"); }), "the latest seek goes out after the command");
    std::this_thread::sleep_for(50ms);
    Check(f.fake->CountCalls("pause") == 1, "a second command in flight is ignored");
    Check(f.fake->CountCalls("seek") == 1, "earlier seeks are dropped");
    Check(WaitUntil([&] { return !f.NowPlaying().commandInFlight; }), "and nothing is in flight afterwards");
}

void StopIsNeverAnEnd()
{
    Fixture f;
    f.PlayAndWatch("h1");
    f.session->Stop();
    Check(WaitUntil([&] { return f.HasCall("stop"); }), "Stop is sent");
    const auto calls = f.fake->Calls();
    const auto told = std::ranges::find(calls, "stoppedByUser h1");
    Check(told != calls.end() && told < std::ranges::find(calls, "stop"), "the engine is told before the command");
}

void SettingsChangedElsewhereShow()
{
    Fixture f;
    std::atomic<int> settingsChanges = 0;
    f.session->SetChangedHandler(
        [&](SessionParts parts)
        {
            if (parts.Has(SessionPart::Settings))
            {
                ++settingsChanges;
            }
        });

    f.fake->ChangeSettingElsewhere("4k");
    Check(WaitUntil([&] { return Part<SettingsState>(*f.session, SessionPart::Settings).encodingPreset == "4k"; }),
        "a setting changed elsewhere shows up");
    Check(WaitUntil([&] { return settingsChanges > 0; }), "and the handler is told");

    f.session->SetSortOrder("name");
    Check(WaitUntil([&] { return Part<SettingsState>(*f.session, SessionPart::Settings).librarySortOrder == "name"; }),
        "the sort order is a setting");
    Check(f.fake->ReadSessionSetting("librarySortOrder") == "name", "kept by the engine");
}

void RestoresWhatTheTvPlays()
{
    Fixture f;
    f.SetTv("Playing", "h2", 300);
    Check(WaitUntil([&] { return f.NowPlaying().fastHash == "h2"; }), "the TV's file is taken back after a relaunch");
    Check(WaitUntil([&] { return f.HasCall("report h2 300"); }), "its progress is reported again");
    Check(f.Autoplay().sectionId == kFolderSection && f.Autoplay().nextFastHash == "h3", "its folder is the playlist");
}

void DeviceSelectionIsRemembered()
{
    Fixture f;
    Check(f.fake->ReadSessionSetting("lastUsedDevice") == kDevice, "the picked device is remembered");
    f.session->SelectDevice("");
    Check(WaitUntil([&] { return f.Devices().selectedReceiverId.empty(); }), "nothing selected");
    f.session->SelectDevice("1.2.3.4:1");
    std::this_thread::sleep_for(30ms);
    Check(f.Devices().selectedReceiverId.empty(), "an unknown device isn't selected");
    f.session->SetAirPlayAvailable(true);
    f.session->RefreshDevices();
    Check(WaitUntil([&] { return f.Devices().devices.size() == 2 && f.Devices().selectedReceiverId == kDevice; }),
        "rediscovery picks the last used device, AirPlay is listed");
}

// The selected TV's TXT id picks its entry in device-profiles.json.
void StreamStartCarriesDeviceKey()
{
    Fixture f;
    f.session->Play("h1", kFolderSection);
    Check(WaitUntil([&] { return f.HasCall("initiate h1 audio=auto sub=auto"); }), "a TV without an id has no key");

    {
        auto lock = f.fake->Lock();
        f.fake->devices[0].castId = "abc";
    }
    f.session->RefreshDevices();
    std::this_thread::sleep_for(50ms);
    f.session->Play("h2", kFolderSection);
    Check(WaitUntil([&] { return f.HasCall("initiate h2 audio=auto sub=auto device=chromecast:abc"); }),
        "the TV's key is passed to the stream start");
}

// ---- Chosen tracks (docs/scope/track-selection-scope.md, section 3) --------

// A card pick is pending until Play, which sends only that kind.
void CardPickSendsOnlyThatKind()
{
    Fixture f;
    f.session->SelectTracks("h2", 1, 4);
    Check(WaitUntil([&] { return f.Library().trackChoices.size() == 1; }), "the card shows the pending pick");
    const auto shown = f.Library().trackChoices.front();
    Check(shown.fastHash == "h2" && shown.audioTrackId == 1 && shown.subtitleTrackId == 4,
        "the picked subtitle over the default audio");
    Check(f.fake->CountCalls("initiate") == 0, "a pick on a card that isn't playing starts nothing");

    f.session->Play("h2", kFolderSection);
    Check(WaitUntil([&] { return f.HasCall("initiate h2 audio=auto sub=4"); }), "only the picked kind is chosen");
    Check(WaitUntil([&] { return f.HasCall("load h2 start=0 sub=4"); }), "the TV gets the stream's subtitle");
    Check(WaitUntil([&] { return f.Library().trackChoices.empty(); }), "the pick is cleared once it plays");
}

// Play's own chosen tracks win over the card's.
void PlayChosenTracksWin()
{
    Fixture f;
    f.session->SelectTracks("h1", 1, 4);
    WaitUntil([&] { return !f.Library().trackChoices.empty(); });
    f.session->Play("h1", kFolderSection, std::nullopt, TrackRequest{2, -1});
    Check(WaitUntil([&] { return f.HasCall("initiate h1 audio=2 sub=-1"); }), "chosen on the command line");
}

// Switching on the playing stream sends the new track and the playing one.
void SwitchOnPlayingSendsBoth()
{
    Fixture f;
    f.PlayAndWatch("h1");
    f.session->SelectSubtitleTrack(3);
    Check(WaitUntil([&] { return f.HasCall("initiate h1 audio=1 sub=3"); }), "the playing audio goes as chosen");

    f.session->SelectTracks("h1", 2, 3);
    Check(WaitUntil([&] { return f.HasCall("initiate h1 audio=2 sub=3"); }), "a card switch on the playing file too");
}

// Preferences go to the backend, and the cards are refreshed.
void PreferenceCommands()
{
    Fixture f;
    const auto before = f.fake->CountCalls("initiate");
    f.session->SetAudioLanguages({"jpn", "eng"});
    f.session->SetSubtitleMode(SubtitleMode::always);
    f.session->SetSubtitleLanguages({"eng"});
    f.session->ForgetFolderTracks();
    Check(WaitUntil([&] { return f.HasCall("forgetFolderTracks"); }), "forget goes to the engine");
    Check(f.HasCall("subtitleMode always"), "the mode goes to the engine");
    const auto settings = Part<SettingsState>(*f.session, SessionPart::Settings);
    Check(settings.subtitleMode == "always" && settings.subtitleLanguages == std::vector<std::string>{"eng"} &&
              settings.audioLanguages == std::vector<std::string>{"jpn", "eng"},
        "the settings show the preferences");
    Check(f.fake->CountCalls("initiate") == before, "nothing plays");
}

// ---- Notices ---------------------------------------------------------------

void EveryCodeHasACatalogEntry()
{
    for (const auto code : {NoticeCode::StreamStartFailed, NoticeCode::DeviceUnreachable,
             NoticeCode::PlaybackFinished, NoticeCode::PlaybackStopped, NoticeCode::FileNotAdded,
             NoticeCode::SettingsFileReset, NoticeCode::Paired, NoticeCode::PairingFailed,
             NoticeCode::PeerConnectionFailed, NoticeCode::SettingsNotApplied,
             NoticeCode::RemoteStreamStarted, NoticeCode::StreamStoppedByPeer, NoticeCode::FfmpegMissing})
    {
        Check(Notices::Find(code) != nullptr, std::format("{} is in the catalog", code));
    }
    Check(Notices::Find("discoveryFailed") == nullptr, "discovery failure is state, not a notice");
    Check(Notices::Find(NoticeCode::SettingsFileReset)->severity == NoticeSeverity::Warning, "a reset is a warning");
}

void NoticeListKeepsTheLastTen()
{
    NoticeList list(30s);
    const auto now = NoticeList::Clock::now();
    for (int i = 0; i < 12; ++i)
    {
        list.Add(NoticeCode::FileNotAdded, {{"path", std::to_string(i)}}, "", now);
    }
    const auto& notices = list.State().notices;
    Check(notices.size() == 10, "ten kept");
    Check(notices.front().params.at("path") == "2" && notices.back().params.at("path") == "11", "newest last");
    Check(notices.back().noticeId == 12, "ids keep counting");
    Check(list.Add("noSuchCode", {}, "", now) == nullptr, "an unknown code isn't added");
}

void NoticeRepeatsCollapse()
{
    NoticeList list(30s);
    const auto now = NoticeList::Clock::now();
    const NoticeParams params{{"title", "Movie"}};
    Check(list.Add(NoticeCode::PlaybackFinished, params, "h1", now) != nullptr, "the first is added");
    Check(list.Add(NoticeCode::PlaybackFinished, params, "h1", now + 10s) == nullptr, "a repeat isn't");
    Check(list.Add(NoticeCode::PlaybackFinished, params, "h2", now + 10s) != nullptr, "another file is");
    Check(list.Add(NoticeCode::PlaybackFinished, params, "h1", now + 31s) != nullptr, "after the window it is");

    const NoticeParams file{{"file", "device-profiles.json"}, {"backup", "device-profiles.json.invalid"}};
    Check(list.Add(NoticeCode::SettingsFileReset, file, "", now) != nullptr, "a reset is added");
    Check(list.Add(NoticeCode::SettingsFileReset, file, "", now + 1h) == nullptr, "once per launch");
}

void MissingFfmpegIsANotice()
{
    Fixture f(true);
    Check(WaitUntil([&] { return f.Notices().size() == 1; }), "shown after start");
    const auto notices = f.Notices();
    Check(!notices.empty() && notices[0].code == "ffmpegMissing" && notices[0].severity == "error",
        "an ffmpegMissing error");

    Fixture withTools;
    Check(withTools.Notices().empty(), "not shown when ffmpeg is there");
}

void TwoNoticesInOneWindowBothArrive()
{
    Fixture f;
    f.fake->ResetFile("device-profiles.json");
    f.fake->ResetFile("stream-profiles.json");
    Check(WaitUntil([&] { return f.Notices().size() == 2; }), "both are kept");

    const auto notices = f.Notices();
    Check(notices[0].code == "settingsFileReset" && notices[0].severity == "warning", "a warning");
    Check(notices[0].params.at("backup") == "device-profiles.json.invalid", "naming the backup");
    Check(notices[0].noticeId + 1 == notices[1].noticeId, "in order");

    f.fake->ResetFile("device-profiles.json");
    std::this_thread::sleep_for(50ms);
    Check(f.Notices().size() == 2, "a file reset again isn't shown again");
}

void RemoteNoticesJoinTheList()
{
    Fixture f;
    f.fake->PostRemoteNotice("streamStoppedByPeer", {{"peer", "Office Mac"}, {"file", "Movie.mkv"}});
    f.fake->PostRemoteNotice("noSuchRemoteCode", {});
    Check(WaitUntil([&] { return f.Notices().size() == 1; }), "the remote notice is listed");

    const auto notices = f.Notices();
    Check(notices[0].code == "streamStoppedByPeer" && notices[0].severity == "warning", "a warning");
    Check(notices[0].systemNotification, "sent as a system notification");
    Check(notices[0].params.at("peer") == "Office Mac", "naming the peer");
    std::this_thread::sleep_for(50ms);
    Check(f.Notices().size() == 1, "an unknown code isn't listed");
}

void AppSettingsCommands()
{
    Fixture f;
    Check(f.Settings().showSystemNotifications && !f.Settings().logDebug, "the defaults");
    f.session->SetLogDebug(true);
    f.session->SetShowSystemNotifications(false);
    Check(WaitUntil([&] { return f.Settings().logDebug && !f.Settings().showSystemNotifications; }),
        "both show in the settings part");
}

}  // namespace

std::vector<TestCase> SessionTestCases()
{
    return {
        {"session: section name order", SectionNameOrder},
        {"session: remote notices join the list", RemoteNoticesJoinTheList},
        {"session: section layout", SectionLayout},
        {"session: autoplay bookkeeping", AutoplayBookkeeping},
        {"session: notifier coalesces", NotifierCoalesces},
        {"session: play starts and casts", PlayStartsAndCasts},
        {"session: head start for a fresh conversion", FreshConversionWaitsReusedDoesNot},
        {"session: subtitle switch in place", SubtitleSwitchesInPlace},
        {"session: start failure", StartFailureShowsNotice},
        {"session: seek to the end", SeekToTheEndReportsFirst},
        {"session: autoplay after the end", FinishedStartsNextAfterCountdown},
        {"session: cancel and now", CancelAndNow},
        {"session: unreadable files", UnreadableFilesAreSkipped},
        {"session: one cast command at a time", OneCastCommandAtATime},
        {"session: stop is never an end", StopIsNeverAnEnd},
        {"session: settings from anywhere", SettingsChangedElsewhereShow},
        {"session: restore from the TV", RestoresWhatTheTvPlays},
        {"session: device selection", DeviceSelectionIsRemembered},
        {"session: stream start carries the device key", StreamStartCarriesDeviceKey},
        {"session: a card pick sends only that kind", CardPickSendsOnlyThatKind},
        {"session: Play's chosen tracks win", PlayChosenTracksWin},
        {"session: a switch on the playing stream sends both", SwitchOnPlayingSendsBoth},
        {"session: preference commands", PreferenceCommands},
        {"session: every notice code has a catalog entry", EveryCodeHasACatalogEntry},
        {"session: the notice list keeps the last ten", NoticeListKeepsTheLastTen},
        {"session: notice repeats collapse", NoticeRepeatsCollapse},
        {"session: notices in one notify window", TwoNoticesInOneWindowBothArrive},
        {"session: missing ffmpeg", MissingFfmpegIsANotice},
        {"session: debug log and system notification settings", AppSettingsCommands},
    };
}
