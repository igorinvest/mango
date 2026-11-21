#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "../../../media/MediaBackend.hpp"
#include "../../library/FileRecord.hpp"
#include "../../MediaInfo.hpp"

// Extracts a file's text subtitle tracks into standalone WebVTT sidecar
// files (<index>.vtt) next to the DASH manifest. Schedules the work (one at
// a time, background, cancel, wait) and has the MediaBackend do each
// extraction as its own job, so it never waits on the video/audio
// conversion.
//
// Every missing text track is extracted in a single ffmpeg pass: reading
// the subtitles means demuxing the whole source file either way, so one
// track costs about as much as all of them. Every text track is declared to
// the Chromecast at LOAD (see Engine::LoadCastDevice) and the server holds a
// request for a not-yet-extracted one until WaitUntilExtracted returns, so a
// later subtitle change switches in place instead of reloading the stream.
//
// Each track is written as <index>.vtt.tmp and only renamed to <index>.vtt
// once ffmpeg finishes successfully — the receiver fetches every declared
// track once at LOAD, so a half-written file must never look extracted.
//
// At most one extraction runs at a time, server-wide.
//
// External subtitle files (see ExternalSubtitleFinder) take a separate path:
// one small ffmpeg run per file, on demand, that never touches the video. A
// broken .srt therefore can't fail the built-in tracks, or the other way
// round. A file whose conversion failed is remembered as broken until it
// changes, and CanExtract stops offering it.
class SubtitleExtractor
{
   public:
    // Blocks until every text track of fileRecord is extracted, external
    // files included. Joins an already-running extraction of the same file
    // rather than starting a second one.
    static void ExtractAll(const FileRecord& fileRecord);

    // Same as ExtractAll, but returns immediately. Built-in tracks only:
    // external files convert in well under a second, so they're converted
    // when first asked for (see WaitUntilExtracted).
    static void ExtractAllInBackground(const FileRecord& fileRecord);

    // Stops an in-flight extraction, discarding its partial output — call
    // before deleting the stream folder it's writing into.
    static void Cancel();

    [[nodiscard]] static bool IsExtracted(const FileRecord& fileRecord, int subtitleTrackIndex);

    // Whether subtitleTrackIndex is one of fileRecord's text tracks — the
    // ones ExtractAll produces a .vtt for — or an external subtitle file
    // that exists and hasn't failed to convert.
    [[nodiscard]] static bool CanExtract(const FileRecord& fileRecord, int subtitleTrackIndex);

    // Blocks until subtitleTrackIndex's .vtt is on disk, extracting every
    // missing track first if needed. False when it never will be: not a
    // text track, ffmpeg failed, or the extraction was cancelled.
    static bool WaitUntilExtracted(const FileRecord& fileRecord, int subtitleTrackIndex);

    // Bitmap subtitles (PGS, DVD, DVB) can't be turned into WebVTT - so
    // they're never offered as a choice (see Engine::GetLibraryEntriesJson,
    // TrackSelector), and one such output would fail the whole multi-track
    // extraction pass.
    [[nodiscard]] static bool IsTextCodec(const std::string& codec);

   private:
    // The built-in half of ExtractAll.
    static void ExtractBuiltInTracks(const FileRecord& fileRecord);

    static void ExtractExternalTracks(const FileRecord& fileRecord);

    // Converts one external subtitle file into its .vtt, synchronously.
    // Conversions run one at a time; Cancel() stops the running one.
    static bool ConvertExternal(const FileRecord& fileRecord, const ExternalSubtitle& subtitle);

    // Converts inputPath into tempPath and waits for it. completed is false
    // when it failed, cancelled true when Cancel() stopped it.
    struct ExternalRunResult
    {
        bool completed = false;
        bool cancelled = false;
    };
    static ExternalRunResult RunExternalConversion(
        const std::filesystem::path& inputPath, const std::filesystem::path& tempPath);

    static void CancelExternal();

    [[nodiscard]] static bool IsBroken(const ExternalSubtitle& subtitle);
    static void MarkBroken(const ExternalSubtitle& subtitle);

    [[nodiscard]] static bool CanExtractExternal(const ExternalSubtitle& subtitle);

    // Returns true when an extraction of fileRecord is running afterwards,
    // false when there was nothing left to extract (or ffmpeg can't start).
    static bool EnsureExtractionRunning(const FileRecord& fileRecord);

    static void CancelLocked();

    [[nodiscard]] static std::vector<SubtitleTrack> MissingTextTracks(const FileRecord& fileRecord);

    static void PublishOutputs(const std::filesystem::path& streamFolder,
        const std::vector<int>& trackIndices, bool completedSuccessfully);

    [[nodiscard]] static std::filesystem::path OutputPath(
        const std::filesystem::path& streamFolder, int subtitleTrackIndex);

    // Each track is written as <index>.vtt.tmp first (see PublishOutputs).
    [[nodiscard]] static std::vector<SubtitleOutput> TempOutputs(
        const std::vector<int>& trackIndices, const std::filesystem::path& streamFolder);
};
