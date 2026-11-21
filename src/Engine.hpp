#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "configuration/CallbackRegister.hpp"
#include "pipeline/TrackRequest.hpp"
#include "pipeline/stream/profiles/PlaylistType.hpp"

namespace mango
{

class Session;
class RemoteStreaming;

// Snapshot of the server's state. The HTTP port is open only while
// something streams (see StreamingSession): ip/port are the last streaming
// session's (empty/0 before the first), isRunning says Start() was called
// and streams can be started, isListening whether the port is open right
// now. Stream URLs come from StreamInitiation::serverUrl, not from here.
struct ServerStatus
{
    std::string ip;
    int port = 0;
    bool isRunning = false;
    bool isListening = false;
};

// A selectable audio/subtitle track. index is the track's real stream index
// (matches VideoTrack/AudioTrack/SubtitleTrack's own .index in MediaInfo.hpp,
// not its position in this array) — that's what InitiateStreamJson expects
// back for audioTrackId/subtitleTrackId, since FileRecord::find_*_by_index
// looks tracks up by this same index, not by array position.
//
// isExternal marks an audio or subtitle file found next to the video rather
// than a track inside it (index >= 1000, see ExternalAudio and
// ExternalSubtitle); fileName is then that file's name relative to the
// video's folder ("Movie.en.srt", "Rus Sound/LostFilm.mka"), for telling
// two same-language files apart.
struct TrackInfo
{
    int index = 0;
    std::string language;
    std::string title;
    bool isExternal = false;
    std::string fileName;
};

// One folder a library entry was found in — see FolderLocation. folderId
// matches a LibraryFolderEntry's folderId. relativePath is the file's path from
// that folder's root and folderPath the subfolder holding it ("" for the
// root itself), both '/'-separated.
struct LibraryEntryLocation
{
    std::string folderId;
    std::string relativePath;
    std::string folderPath;
};

// A folder added with Add Folder, for a native UI to build its folder
// sections from (see Engine::GetLibraryFoldersJson). name is the folder's
// own name, for the section header.
struct LibraryFolderEntry
{
    std::string folderId;
    std::string name;
    std::string rootPath;
    std::int64_t addedAtUnixSeconds = 0;
};

// Snapshot of a library entry for display in a native UI. Deliberately
// mirrors only what a UI needs, not FileRecord's/MediaInfo's full shape.
struct LibraryEntry
{
    std::string fastHash;
    std::string title;
    std::string filePath;
    int videoWidth = 0;
    int videoHeight = 0;
    std::string videoCodec;
    std::vector<TrackInfo> audioTracks;
    std::vector<TrackInfo> subtitleTracks;

    // The tracks Play starts with when the user picks none, so a UI shows
    // them pre-selected — see TrackSelector: the file's last played tracks,
    // its folder's remembered languages, the language preferences, the
    // file's default flags, the first track, in that order. A real stream
    // index (that track's own .index, not its array position), or -1 when
    // the list is empty — and for subtitles also when they'd be off.
    int defaultAudioTrackId = -1;
    int defaultSubtitleTrackId = -1;

    // How far into this file a previous playback session reached — see
    // Engine::ReportPlaybackProgress. 0 if never played, or played for less
    // than PlaybackHistory::minimumResumeSeconds. For showing how much has
    // been watched; Play starts from resumeStartSeconds instead.
    double lastPositionSeconds = 0.0;

    // Where a UI's Play should start: lastPositionSeconds, or 0 once the
    // file is fully watched (isFullyWatched) — resuming a finished movie a
    // second before its end would stop playback instantly.
    double resumeStartSeconds = 0.0;

    // Watched (nearly) all the way through — see PlaybackHistory::
    // completedFraction.
    bool isFullyWatched = false;

    // When playback of this file was last reported, as Unix seconds. 0 if
    // never played.
    std::int64_t lastPlayedAtUnixSeconds = 0;

    // This file's total length, from ffprobe (see MediaInfo::durationSec).
    // Paired with lastPositionSeconds so a UI can show how much of the file
    // has already been watched (lastPositionSeconds / durationSeconds).
    double durationSeconds = 0.0;

    // Absolute local filesystem path to this file's extracted embedded
    // cover/poster image (see CoverExtractor), or empty if it has none. A
    // native UI running on the same machine as the engine can load this
    // directly. Also served as /cover/<streamId> while it streams, for the Chromecast's
    // loading screen (see LoadCastDevice).
    std::string coverPath;

    // True only if this file was added during the server's current run,
    // i.e. it wasn't already in library.json when the server started - see
    // FileRecord::addedThisSession. Lets a UI show a "New" badge that
    // clears itself on the next restart rather than persisting forever.
    bool addedThisSession = false;

    // When this file was first added to the library, as Unix seconds (see
    // FileRecord::addedAtUnixSeconds) — for a UI's "Date Added" sort. 0 if
    // unknown.
    std::int64_t addedAtUnixSeconds = 0;

    // Whether this file was added with Add Movie (UIs list these under
    // "Added Manually"), and every added folder it was found in. A file can
    // be both, and can be in several folders — UIs show it in each place.
    bool addedManually = true;
    std::vector<LibraryEntryLocation> folderLocations;

    // Set for a video in a paired device's library (see
    // RemoteStreaming::GetLibraryEntriesJson): that device's id and name.
    // fastHash is then "remote:<remotePeerId>:<its fastHash there>", and
    // every Engine call taking a fastHash plays it through that device.
    // Such an entry has no filePath and isn't addedManually.
    std::string remotePeerId;
    std::string remotePeerName;
};

// A single Chromecast (Google Cast) receiver found via network discovery.
// AirPlay discovery isn't implemented yet.
struct CastDevice
{
    std::string name;
    std::string ipAddress;
    int port = 0;

    // The TV's TXT "id", stable across renames and IP changes; empty when
    // it doesn't send one. Picks its entry in device-profiles.json.
    std::string castId;
};

// Result of one DiscoverCastDevicesJson call. `succeeded` is false when the
// mDNS query itself couldn't be sent (e.g. no usable network interface) -
// distinct from a query that went out fine but simply found no receivers,
// which is `succeeded == true` with an empty `devices`. Native UIs use this
// to show a real discovery-failed error instead of "no devices found".
struct CastDiscoveryResult
{
    std::vector<CastDevice> devices;
    bool succeeded = true;
};

// Snapshot of a Chromecast's current playback, for display in a native UI.
// state is one of "Unreachable"/"Idle"/"Active"/"Buffering"/"Playing"/
// "Paused" (mirrors mango::ChromecastPlaybackState's case names). isFromThis
// Server tells the UI whether this process's own library/track data applies
// to what's playing — when false (another server on the network started the
// cast), audio/subtitle track info genuinely isn't available here, since
// track state lives in whichever process's StreamPipeline is actually
// serving the content.
struct CastPlaybackStatus
{
    std::string state;
    std::string appDisplayName;
    std::string mediaTitle;
    double currentTimeSeconds = 0.0;
    double durationSeconds = 0.0;

    // The duration as the receiver itself reports it, before durationSeconds
    // swaps in the library's (see FinishedConversionDuration): -1 while it
    // treats the stream as live, 0 with no media. Diagnostics only — shows
    // whether the TV picked up the final manifest.
    double tvDurationSeconds = 0.0;
    bool isFromThisServer = false;

    // False when the receiver's status left out which media it plays (it
    // does mid-seek): isFromThisServer then says nothing about the content.
    bool hasMediaInfo = true;

    // The library file the TV is playing, looked up from the stream ID in
    // its URL (".../stream/<streamId>/..."), so an app relaunched
    // mid-playback can pick the session back up. Empty unless
    // isFromThisServer and that stream is still the current one.
    std::string fastHash;

    // With state "Idle" while a media session is still loaded: why it
    // stopped — "FINISHED", "CANCELLED", "INTERRUPTED" or "ERROR". Empty
    // otherwise. Pass the whole status to Engine::ClassifyCastPlaybackEnd
    // rather than reading this directly.
    std::string idleReason;
};

// Result of Engine::InitiateStreamJson — relative paths (e.g.
// "<streamId>/index.mpd") under the server's /stream/. success is false when the file wasn't found in the library or nothing
// could be started; subtitleAddress is empty when none was requested/
// produced.
struct StreamInitiation
{
    bool success = false;

    // The stream's random ID (see StreamId): the first path segment of
    // every URL it's served under. A subtitle track's URL is
    // "/stream/<streamId>/<index>.vtt". It stops working once the stream is
    // stopped or replaced.
    std::string streamId;

    // "http://<ip>:<port>", the server's address on the current network,
    // where it listens for this stream. Prefix it to
    // "/stream/<streamAddress>"; don't reuse an address read earlier — the
    // network and the port can differ between streams.
    std::string serverUrl;
    std::string streamAddress;
    std::string subtitleAddress;

    // True when this started a fresh ffmpeg conversion; false when it
    // reused one already running or finished for the same file and audio
    // track (including one kept from before an app restart). Native apps
    // only wait GetStreamStartDelayMs before playing a fresh one.
    bool startedNewTranscode = false;

    // The tracks the stream plays — the chosen ones, and what the engine
    // picked for the rest (see TrackSelector). -1 for none (subtitles off).
    int audioTrackId = -1;
    int subtitleTrackId = -1;

    // Why no stream was started, when success is false: "noVideoEncoder",
    // "ffmpegMissing" (Linux: no ffmpeg found at startup),
    // "unsupportedCodec", "fileUnreadable", "outputFailed" or "unknown"
    // (see ConversionErrors::ToString). Empty on success.
    std::string error;
};

// Result of Engine::GetTranscodeStatusJson: which library file the current
// stream belongs to and how much of it is converted. fastHash is empty and
// percent -1 when nothing is streaming. A finished conversion stays at 100
// until another stream replaces it — that movie is ready to play instantly.
struct TranscodeStatus
{
    std::string fastHash;
    double percent = -1.0;

    // Set (with fastHash, and percent -1) when the current stream's
    // conversion failed - same values as StreamInitiation::error.
    std::string error;
};

// Public entry point into the Mango engine. This is the only header a
// consuming app (test harness or a native macOS/Windows shell) should need —
// everything else (httplib, glaze, the HTTP routes, the stream pipeline) is
// an internal implementation detail and stays out of this include path.
class Engine
{
   public:
    Engine();
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Makes the engine ready to stream and returns immediately. The HTTP
    // port isn't opened here: each InitiateStreamJson opens it on the
    // current LAN address, and it closes again once playback is over (see
    // ServerStatus). Safe to call once; a second call before Stop() is a
    // no-op.
    void Start();

    // Closes the HTTP port if open and blocks until it has fully shut down. Deliberately
    // leaves any in-flight transcode running — see ShutdownCoordinator for
    // that, which is a separate, intentionally-independent lifecycle.
    void Stop();

    ServerStatus GetStatus() const;

    // Registers the native "pick a video file" dialog. If unset, SelectFile
    // logs a warning and adds nothing.
    void SetOpenFileHandler(CallbackRegister::OpenFileHandler handler);

    // Registers the native "pick a folder" dialog. If unset, SelectFolder
    // logs a warning and adds nothing.
    void SetOpenFolderHandler(CallbackRegister::OpenFolderHandler handler);

    // Registers the native secret store (Keychain, Credential Manager) that
    // keeps this device's remote streaming key. Call before Start. If unset,
    // the key stays in remote/identity.json, readable only by the user. A
    // key already in that file moves into the store.
    void SetSecretStoreHandlers(CallbackRegister::SecretStoreHandlers handlers);

    // Base64-encoded security-scoped bookmarks (macOS) for every file and
    // every added folder currently in the library. Call at startup, before accepting requests,
    // and start accessing each one — NSOpenPanel's grant from whichever
    // launch a file was added in doesn't survive a restart.
    std::vector<std::string> GetSecurityBookmarks() const;

    // Evicts library entries whose file is genuinely gone, then queues a
    // background re-scan of every added folder (see RescanLibraryFolders).
    // Call once, after every bookmark from GetSecurityBookmarks() has been
    // resolved *and* had -startAccessingSecurityScopedResource called on
    // it — calling it any earlier would mistake "sandboxed, not accessible
    // yet" for "deleted" and evict (and lose the bookmark for) every
    // library entry.
    void PruneMissingLibraryFiles();

    // Opens the native "select a video file" dialog (via the registered
    // OpenFileHandler) and adds the chosen file to the library. Returns
    // immediately; the add itself completes asynchronously once the user
    // picks a file.
    void SelectFile();

    // Opens the native "select a folder" dialog (via the registered
    // OpenFolderHandler) and adds the chosen folder to the library, then
    // scans it on a background thread: every supported video anywhere
    // under it is added. Returns immediately. Picking a folder that's
    // already added (or inside one) just re-scans it; added folders inside
    // the chosen one are merged into it. Progress: see
    // GetLibraryScanStatusJson.
    void SelectFolder();

    // Adds the video at filePath (UTF-8) to the library, no dialog: probes
    // it, extracts its cover and persists the library before returning.
    // Returns false if it's unreadable or not a playable video. No
    // security-scoped bookmark — for clients without a picker (the CLI).
    bool AddFile(const std::string& filePath);

    // Adds the folder at folderPath (UTF-8) like SelectFolder does, no
    // dialog. Returns immediately; the scan runs in the background.
    void AddFolder(const std::string& folderPath);

    // Queues a background re-scan of every added folder — picks up new
    // videos, follows moved ones and drops deleted ones. Unchanged files
    // (same size and modification time) aren't re-read. For a UI's Refresh.
    void RescanLibraryFolders();

    // Current contents of the library, as a JSON array of LibraryEntry (see
    // its member names above for the exact keys — glaze serializes plain
    // aggregates using their member names as-is). A plain JSON string keeps
    // this crossing the native-bridge boundary as a single value rather than
    // needing a parallel ObjC/C# mirror type per struct.
    [[nodiscard]] std::string GetLibraryEntriesJson() const;

    // Every folder added with SelectFolder or AddFolder, as a JSON array of
    // LibraryFolderEntry. UIs build their folder sections from this plus
    // each LibraryEntry's folderLocations.
    [[nodiscard]] std::string GetLibraryFoldersJson() const;

    // What the folder scanner is doing right now, as JSON:
    // {"scanning":bool,"folderId":"...","folderName":"...","filesDone":N,
    // "filesTotal":N,"foldersQueued":N}. filesTotal is 0 while the folder
    // is still being walked. For a UI's "Scanning Movies… 34/120".
    [[nodiscard]] std::string GetLibraryScanStatusJson() const;

    // Scans the local network for Chromecast receivers (mDNS) and returns
    // what it found as a JSON-serialized CastDiscoveryResult (devices +
    // succeeded). Blocks the calling thread for up to timeoutMs — call this
    // off the main/UI thread.
    [[nodiscard]] std::string DiscoverCastDevicesJson(int timeoutMs = 3000) const;

    // Connects directly to a Chromecast (found via DiscoverCastDevicesJson)
    // and asks what it's currently playing, as JSON — see CastPlaybackStatus
    // for the shape. Blocks the calling thread for up to timeoutMs — call
    // this off the main/UI thread.
    [[nodiscard]] std::string QueryCastPlaybackStatusJson(
        const std::string& ipAddress, int port, int timeoutMs = 3000) const;

    // Sends a playback command to a Chromecast over its CastV2 control
    // channel — works regardless of which process (this one or another
    // server on the network) started the cast, unlike track selection.
    // Every command first resolves the receiver's active media session, so
    // it only does anything while some media is actually loaded. Returns
    // one of "Success"/"SentUnconfirmed"/"NoActiveMedia"/"Unreachable"
    // (mirrors mango::ChromecastCommandResult's case names). Blocks the
    // calling thread for up to timeoutMs — call these off the main/UI
    // thread.
    [[nodiscard]] std::string PlayCastDevice(
        const std::string& ipAddress, int port, int timeoutMs = 3000) const;
    [[nodiscard]] std::string PauseCastDevice(
        const std::string& ipAddress, int port, int timeoutMs = 3000) const;

    // Stops the current media session (equivalent to a media-level "stop",
    // not closing the receiver app itself).
    [[nodiscard]] std::string StopCastDevice(
        const std::string& ipAddress, int port, int timeoutMs = 3000) const;

    // Also ends a pending resume (see LoadCastDevice) — after the user
    // seeks, wherever playback is reported next is real.
    [[nodiscard]] std::string SeekCastDevice(
        const std::string& ipAddress, int port, double positionSeconds, int timeoutMs = 3000) const;

    // Starts playing contentUrl fresh on a Chromecast, launching its Default
    // Media Receiver if nothing's running (or reusing it if it already is).
    // Unlike Play/Pause/Stop/Seek, doesn't require anything to already be
    // loaded — this is what gets a stream (e.g. from InitiateStreamJson)
    // onto the TV in the first place. contentType is a MIME type (e.g.
    // "application/dash+xml" for the DASH manifests this server produces);
    // title is shown on the receiver's now-playing screen. fastHash
    // identifies which library file this is (see InitiateStreamJson) —
    // used only to check which of subtitleTracksJson's candidates actually
    // have an extracted file yet, and to send its cover (when it has one) as
    // the poster the receiver shows while loading; not sent to the device
    // itself.
    // subtitleTracksJson is a JSON array of every subtitle track the file
    // has — [{"index":N,"contentUrl":"..."}, ...], empty/malformed treated
    // as none. Every text track among them is declared to the receiver,
    // extracted or not, so SetCastSubtitleTrack can later switch to any of
    // them in place. The Default Media Receiver eagerly fetches every
    // declared track's contentUrl at LOAD time, and a single 404 among them
    // turns subtitles off entirely — so the server holds a request for a
    // track still being extracted until its .vtt exists, and bitmap tracks
    // (which never get one) aren't declared at all.
    // activeSubtitleTrackIndex (-1 for none) picks which declared track
    // starts active. startTimeSeconds resumes playback there instead of
    // from the beginning — LibraryEntry::resumeStartSeconds for a fresh
    // Play, or the receiver's current position when reloading for a
    // subtitle change that couldn't switch in place. Until the receiver
    // actually reaches startTimeSeconds, ReportPlaybackProgress ignores
    // reports from well before it (see PlaybackHistory::BeginSession), so
    // the ~0 a receiver can report while it buffers doesn't overwrite the
    // remembered position. Returns one
    // of "Success"/"SentUnconfirmed"/"Unreachable". A cold app launch can
    // take a few seconds, hence the longer default timeout than the other
    // cast commands. Blocks the calling thread for up to timeoutMs — call
    // this off the main/UI thread.
    [[nodiscard]] std::string LoadCastDevice(const std::string& ipAddress, int port,
        const std::string& contentUrl, const std::string& contentType, const std::string& title,
        const std::string& fastHash, const std::string& subtitleTracksJson,
        int activeSubtitleTrackIndex, double startTimeSeconds = 0.0, int timeoutMs = 6000) const;

    // Switches which subtitle track (if any) is showing on an already-
    // casting session without restarting playback — see ChromecastControl::
    // SetActiveSubtitleTrack. subtitleTrackIndex must have been declared in
    // that session's LoadCastDevice call (pass -1 to turn subtitles off) —
    // i.e. be a text track of fastHash's file. If it isn't, this returns
    // "TrackNotAvailable" without contacting the device at all. Callers
    // should fall back to InitiateStreamJson + LoadCastDevice in that case.
    // Otherwise returns one of "Success"/"SentUnconfirmed"/"NoActiveMedia"/
    // "Unreachable". Blocks the calling thread for up to timeoutMs — call
    // this off the main/UI thread.
    [[nodiscard]] std::string SetCastSubtitleTrack(const std::string& ipAddress, int port,
        const std::string& fastHash, int subtitleTrackIndex, int timeoutMs = 3000) const;

    // Starts (or reuses an already-running) transcode/stream for a library
    // file, in-process since this server owns the pipeline directly.
    // tracks holds the tracks the user chose, as indices from the matching
    // LibraryEntry's audioTracks/subtitleTracks (see TrackInfo) — subtitle
    // -1 for no subtitles. A kind left std::nullopt is picked by the engine
    // (see TrackSelector), and only a chosen kind is remembered for the
    // file's folder. The result says which tracks play. Changing the audio track,
    // or switching playlist, from what's currently running restarts the whole
    // pipeline; a subtitle-only change is cheaper. playlist picks the stream
    // kind: DASH (the default, for Chromecast) or HLS (for local
    // AVPlayer/AirPlay playback). deviceKey ("chromecast:<id>", see
    // CastDevice::castId) applies that TV's settings from device-profiles.json;
    // empty uses the defaults. Returns JSON — see StreamInitiation for the
    // shape.
    [[nodiscard]] std::string InitiateStreamJson(const std::string& fastHash, const TrackRequest& tracks,
        PlaylistType playlist = PlaylistType::DASH, const std::string& deviceKey = {}) const;

    // Every language the engine knows, as [{"code": "eng", "name":
    // "English"}, ...] sorted by name — for the language preference pickers
    // (see Session::SetAudioLanguages).
    [[nodiscard]] std::string GetLanguagesJson() const;

    // Current global video-transcode-mode setting — one of "automatic"
    // (capability-based copy-vs-decode decision, the default), "forceCopy",
    // or "forceDecode" (see VideoTranscodeModeSetting). Applies to every
    // subsequent InitiateStreamJson call server-wide — it's a standing
    // preference, not a per-stream parameter.
    [[nodiscard]] std::string GetVideoTranscodeMode() const;

    // Sets the global video-transcode-mode setting — same three values as
    // GetVideoTranscodeMode(). An unrecognized value leaves the setting
    // unchanged.
    void SetVideoTranscodeMode(const std::string& mode);

    // Current global target quality for converted video - "720p", "1080p"
    // (the default) or "4k" (see EncodingPresetSetting). Caps the output
    // size (and what Automatic mode copies) and picks the bitrates, for
    // every subsequent InitiateStreamJson call.
    [[nodiscard]] std::string GetEncodingPreset() const;

    // Sets the target quality - same three values as GetEncodingPreset().
    // An unrecognized value leaves the setting unchanged.
    void SetEncodingPreset(const std::string& preset);

    // What the Supported Formats window shows for playlist's profile -
    // Chromecast (DASH) or AirPlay (HLS):
    //   {"video": [{"codec": "h264", "maxResolution": "4k"}, ...],
    //    "audio": [{"codec": "aac", "supported": true}, ...],
    //    "rotationSupported": false,
    //    "conversion": [{"codec": "h264", "driver": "videotoolbox",
    //        "encoder": "h264_videotoolbox", "software": false, "external": false}]}
    // maxResolution is "720p"/"1080p"/"4k", "custom" (a hand-edited size
    // matching no preset) or null (not supported). See SupportedFormats.
    // "conversion" lists what this machine converts video to, best first,
    // and is empty when it can't; it waits for the encoder probe.
    [[nodiscard]] std::string GetSupportedFormatsJson(PlaylistType playlist) const;

    // The media tools the engine found (one line per ffmpeg install), for
    // the console's startup report. Waits for the background probe's
    // install stage, which Start begins.
    [[nodiscard]] std::vector<std::string> GetMediaTools() const;

    // Each of these writes stream-profiles.json straight away; the change
    // applies to the next stream that starts. Codecs outside the window's
    // list (SupportedFormats::kVideoCodecs/kAudioCodecs) are ignored.
    //
    // maxResolution "720p"/"1080p"/"4k" replaces the codec's entries with
    // its built-in ones for that size; std::nullopt (Not supported) removes
    // them. An unrecognized value is ignored.
    void SetVideoCodecMaxResolution(
        PlaylistType playlist, const std::string& codec, const std::optional<std::string>& maxResolution);
    void SetAudioCodecSupported(PlaylistType playlist, const std::string& codec, bool supported);
    void SetRotationSupported(PlaylistType playlist, bool supported);
    void RestoreSupportedFormatDefaults(PlaylistType playlist);

    // How long a native app should wait after InitiateStreamJson succeeds
    // before handing the stream to a player (the AirPlay sheet, or
    // LoadCastDevice), in milliseconds — so ffmpeg has the first segments
    // ready when they're requested. From settings.json's
    // "streamStartDelayMs" (default 3000, clamped to 0-30000); see
    // StreamStartDelaySetting.
    [[nodiscard]] int GetStreamStartDelayMs() const;

    // Whether the current stream's conversion has reached settings.json's
    // "streamStartReadyPercent" (default 10) — native apps poll this during
    // the GetStreamStartDelayMs wait and hand the stream over as soon as it
    // turns true, instead of always waiting the full delay.
    [[nodiscard]] bool IsStreamStartReady() const;

    // Records how far into fastHash's playback a client actually reached,
    // plus which audio/subtitle track were active (subtitleTrackId -1 for
    // "off") — meant to be called periodically (e.g. every playback-status
    // poll tick) while something plays, from whichever client is driving
    // it (Chromecast control or a local AirPlay player). The next
    // GetLibraryEntriesJson call reflects this via LibraryEntry's
    // lastPositionSeconds/resumeStartSeconds/isFullyWatched/
    // defaultAudioTrackId/defaultSubtitleTrackId, so a later
    // InitiateStreamJson + LoadCastDevice/local player can resume from the
    // same spot with the same tracks. Every rule about what's remembered
    // lives in PlaybackHistory::Report. No-op if fastHash isn't in the
    // library.
    void ReportPlaybackProgress(
        const std::string& fastHash, double positionSeconds, int audioTrackId, int subtitleTrackId);

    // How much of the currently-initiated stream's source file ffmpeg has
    // fully transcoded, as a percentage (0-100) — see StreamManager::
    // GetTranscodeProgressPercent. Distinct from lastPositionSeconds/
    // ReportPlaybackProgress, which track how far a client has watched, not
    // how far the transcode itself has gotten. Returns -1 if nothing is
    // currently streaming (either never started, or stopped since).
    [[nodiscard]] double GetTranscodeProgressPercent() const;

    // GetTranscodeProgressPercent plus the fastHash it belongs to, as a
    // JSON-serialized TranscodeStatus — lets a UI attribute progress to the
    // right movie even for a stream it didn't start itself (one restored
    // after a restart, or started by another app).
    [[nodiscard]] std::string GetTranscodeStatusJson() const;

    // Seconds since the HTTP server last served a stream file to any client
    // — see StreamActivity. A playing client keeps requesting segments, so
    // native apps treat a recent request as "something is playing" (to keep
    // the machine awake), however that playback was started. -1 if nothing
    // has been served since launch.
    [[nodiscard]] double GetSecondsSinceLastStreamRequest() const;

    // Whether fastHash's playback on a Chromecast finished (watched to the
    // end) or stopped any other way, for Play-next-in-folder. statusJson is
    // a QueryCastPlaybackStatusJson result. Returns "Playing", "Finished",
    // "Stopped" or "Unknown" (unreachable, or nothing to go on). Finished
    // needs the receiver's end (IDLE/FINISHED, or the session gone) *and*
    // the last ReportPlaybackProgress position within max(60 s, 3%) of the
    // ffprobe duration — so a conversion that died halfway, which a
    // receiver also plays "to the end", is Stopped. A Stop sent through
    // NotifyPlaybackStoppedByUser makes it Stopped too. Doesn't contact the
    // device; the same status keeps giving the same answer, so callers act
    // on the first Finished only.
    [[nodiscard]] std::string ClassifyCastPlaybackEnd(
        const std::string& fastHash, const std::string& statusJson) const;

    // The same, for a player the engine can't see (AirPlay's AVPlayer).
    // endReason is "Ended", "Stopped" or "Error"; positionSeconds is where
    // the player was when it ended.
    [[nodiscard]] std::string ClassifyLocalPlaybackEnd(
        const std::string& fastHash, double positionSeconds, const std::string& endReason) const;

    // Call from the app's Stop: whatever the player says next about
    // fastHash is "Stopped", never "Finished", until fastHash is started
    // again (InitiateStreamJson).
    void NotifyPlaybackStoppedByUser(const std::string& fastHash);

    // "Play next video in folder automatically" — see AutoplaySetting.
    // Default true.
    [[nodiscard]] bool GetAutoplayNextInFolder() const;
    void SetAutoplayNextInFolder(bool enabled);

    // "Listen only while streaming" (default true; see ListenModeSetting):
    // the HTTP port opens with a stream and closes after playback. Off
    // keeps it open while the engine runs. Applies right away: turning it
    // off listens now; turning it on closes the port once nothing is being
    // fetched.
    [[nodiscard]] bool GetListenOnlyWhileStreaming() const;
    void SetListenOnlyWhileStreaming(bool enabled);

    // Writes one line into the engine's log, so the app's decisions and the
    // engine's share one timeline (see docs/logging-scope.md). level is
    // "Error", "Warning", "Info" or "Debug" (unknown: Info); category is one
    // of the app's: "App", "Playlist", "UI". While a stream is current the
    // line lands in its folder's mango.log, next to ffmpeg.log. Never blocks
    // on I/O; a disabled Debug line costs only the call.
    void Log(const std::string& level, const std::string& category, const std::string& message);

    // The app's version, for each stream log's header line. Call once at
    // startup.
    void SetAppVersion(const std::string& version);

    // Where settings.json and the hand-edited files (device-profiles.json,
    // encoding-defaults.json, stream-profiles.json) are: the app data
    // folder. UTF-8.
    [[nodiscard]] std::string GetSettingsFolder() const;

    // The stream folders, each with its mango.log and ffmpeg.log. UTF-8;
    // the folder may not exist before the first stream.
    [[nodiscard]] std::string GetLogsFolder() const;

    // The app-level state and flows the apps share — stream start, the
    // TV's status, cast commands, autoplay, live change notifications (see
    // session/Session.hpp). Started by Start(); lives as long as the
    // engine.
    [[nodiscard]] Session& GetSession();

    // Remote streaming between two Mango apps: pairing and the encrypted
    // link to each paired device (see remote/RemoteStreaming.hpp). Started
    // by Start(); lives as long as the engine.
    [[nodiscard]] RemoteStreaming& GetRemote();
    [[nodiscard]] const RemoteStreaming& GetRemote() const;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}  // namespace mango
