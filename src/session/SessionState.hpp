#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "Engine.hpp"

namespace mango
{

// The parts of the session's state an app reads, each as its own JSON
// snapshot (see Session::GetPartJson). A change marks its part dirty, and
// the change handler is told which parts changed (see SessionParts).
enum class SessionPart : std::uint32_t
{
    Server = 0,
    Library,
    Transcode,
    Devices,
    NowPlaying,
    Autoplay,
    Settings,
    Notice,
    Count
};

// A bit set of SessionPart: bit N is SessionPart N. Crosses the C bridge
// as a plain uint32_t (see Bits/FromBits).
class SessionParts
{
   public:
    constexpr SessionParts() = default;
    constexpr SessionParts(SessionPart part) : m_bits(Bit(part)) {}  // NOLINT: implicit on purpose

    static constexpr SessionParts All() { return FromBits((1u << static_cast<std::uint32_t>(SessionPart::Count)) - 1u); }
    static constexpr SessionParts FromBits(std::uint32_t bits)
    {
        SessionParts parts;
        parts.m_bits = bits;
        return parts;
    }

    [[nodiscard]] constexpr bool Has(SessionPart part) const { return (m_bits & Bit(part)) != 0; }
    [[nodiscard]] constexpr bool Empty() const { return m_bits == 0; }
    [[nodiscard]] constexpr std::uint32_t Bits() const { return m_bits; }

    constexpr SessionParts& operator|=(SessionParts other)
    {
        m_bits |= other.m_bits;
        return *this;
    }
    friend constexpr SessionParts operator|(SessionParts a, SessionParts b) { return a |= b; }
    friend constexpr bool operator==(SessionParts, SessionParts) = default;

   private:
    static constexpr std::uint32_t Bit(SessionPart part) { return 1u << static_cast<std::uint32_t>(part); }

    std::uint32_t m_bits = 0;
};

// Called on the session thread, never on the app's UI thread. Must return
// quickly: hop to the UI thread, then read the changed parts there with
// Session::GetPartJson.
using SessionChangedHandler = std::function<void(SessionParts changed)>;

// "server": ServerStatus (see Engine.hpp).

// One full-width section of the library grid: the videos added with Add
// Movie (kind "manual", sectionId "manual"), one folder or subfolder of an added
// folder (kind "folder"), or a paired device's library (kind "remote", sectionId
// "remote:<deviceId>", breadcrumb the device's name). Sections come in display order for the
// current sort; fastHashes are the videos directly inside, sorted too.
// parentSectionId is the enclosing folder's section ("" for an added folder and for
// "manual") — for search, which keeps a folder whose subfolders match.
// sectionIds are stable across refreshes and launches (collapsed state is kept by
// them in the apps).
struct LibrarySection
{
    std::string sectionId;
    std::string kind;
    std::vector<std::string> breadcrumb;
    std::string parentSectionId;
    std::vector<std::string> fastHashes;
};

// What the folder scanner is doing (see Engine::GetLibraryScanStatusJson).
struct LibraryScanInfo
{
    bool scanning = false;
    std::string folderId;
    std::string folderName;
    int filesDone = 0;
    int filesTotal = 0;
    int foldersQueued = 0;
};

// The audio/subtitle tracks a file's card shows (subtitleTrackId -1 for
// off). In LibraryState: only files with a pick not played yet (see
// PendingTrackPicks) — the picked kind as picked, the other as the entry's
// default — shown instead of the entry's defaults.
struct TrackChoice
{
    std::string fastHash;
    int audioTrackId = -1;
    int subtitleTrackId = -1;
};

// "library"
struct LibraryState
{
    std::vector<LibraryEntry> entries;
    std::vector<LibraryFolderEntry> folders;
    std::vector<LibrarySection> sections;
    LibraryScanInfo scan;
    std::vector<TrackChoice> trackChoices;
};

// "transcode": TranscodeStatus (see Engine.hpp).

// A receiver the app can pick. kind is "chromecast" or "airplay"; receiverId is
// "<ip>:<port>" for a Chromecast and "airplay" for AirPlay.
struct SessionDevice
{
    std::string receiverId;
    std::string kind;
    std::string name;
    std::string ipAddress;
    int port = 0;
};

// "devices": the discovered Chromecasts, then AirPlay when the app offers
// it (see Session::SetAirPlayAvailable). selectedReceiverId is "" when nothing is
// selected.
struct DevicesState
{
    std::vector<SessionDevice> devices;
    bool discovering = false;
    bool discoveryFailed = false;
    std::string selectedReceiverId;
};

// "nowPlaying": what this session last started and what the selected TV
// reports.
//
// state: "none" (nothing started, no TV status), "starting" (the stream is
// being prepared), "loading" (sent to the TV, waiting for its answer),
// "buffering", "playing", "paused", "idle", "unreachable", or "local" (an
// AirPlay player in the app). tvState is the TV's own state, "" without a
// status.
//
// startWait*UnixMs: while a fresh conversion gets its head start before the
// stream is handed over (0 otherwise), for a progress bar.
//
// localStreamUrl: the HLS stream an AirPlay player plays, from
// localStartSeconds. localPlaybackId changes with each new one, so the
// player loads it even when it's the same file again.
//
// shouldPreventSleep: something is being served or about to be; the app
// keeps the computer awake while it's set.
struct NowPlayingState
{
    std::string state = "none";
    std::string target;
    std::string deviceId;
    std::string fastHash;
    std::string title;
    int audioTrackId = -1;
    int subtitleTrackId = -1;
    std::string tvState;
    std::string tvMediaTitle;
    std::string tvAppDisplayName;
    double positionSeconds = 0.0;
    double durationSeconds = 0.0;
    bool isFromThisServer = false;
    bool commandInFlight = false;
    std::int64_t startWaitBeginUnixMs = 0;
    std::int64_t startWaitEndUnixMs = 0;
    std::string localStreamUrl;
    double localStartSeconds = 0.0;
    std::uint64_t localPlaybackId = 0;
    bool shouldPreventSleep = false;
};

// "autoplay": the next video of the section playing now (nextFastHash ""
// when there's none, with noNextReason "noSection" or "lastInSection"), and
// the countdown to it once the current one finished.
struct AutoplayState
{
    std::string sectionId;
    std::string nextFastHash;
    std::string nextTitle;
    std::string noNextReason;
    bool countdownActive = false;
    std::int64_t countdownDeadlineUnixMs = 0;
};

// "settings". librarySortOrder is "name" or "dateAdded".
struct SettingsState
{
    std::string videoTranscodeMode = "automatic";
    std::string encodingPreset = "1080p";
    bool autoplayNextInFolder = true;
    bool listenOnlyWhileStreaming = true;
    std::string librarySortOrder = "dateAdded";

    // Debug-level log lines (applies at the next launch).
    bool logDebug = false;
    // The apps send some notices as system notifications while their window
    // isn't active (see SessionNotice::systemNotification).
    bool showSystemNotifications = true;

    // Track preferences (see TrackPreferences): canonical language codes,
    // best first, and one of "off", "foreignAudioOnly", "always",
    // "forcedOnly".
    std::vector<std::string> audioLanguages;
    std::string subtitleMode = "foreignAudioOnly";
    std::vector<std::string> subtitleLanguages;
};

// Named values an app puts into a notice's text (a file name, a TV's name,
// an error code). See NoticeCode in Notices.hpp for each code's params.
using NoticeParams = std::map<std::string, std::string>;

// One message for the user: an event, shown once (see
// docs/scope/user-notifications-scope.md). severity is "info", "warning" or
// "error", from the catalog (Notices.hpp). fastHash is the file it's about,
// "" for none. time is UTC, "2026-11-19T21:04:11Z". systemNotification: the
// app may send it as a system notification while its window isn't active.
// The apps turn code and params into text.
struct SessionNotice
{
    std::uint64_t noticeId = 0;
    std::string code;
    std::string severity;
    NoticeParams params;
    std::string fastHash;
    std::string time;
    bool systemNotification = false;
};

// "notice": the last 10 notices, newest last. noticeId increases with each,
// so an app shows every notice above the last one it showed, and none is lost
// between two notifications.
struct NoticeState
{
    std::vector<SessionNotice> notices;
};

}  // namespace mango
