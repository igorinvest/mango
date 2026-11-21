#pragma once

#include <expected>
#include <optional>
#include <string>

#include "../media/MediaJob.hpp"
#include "stream/target/StreamTarget.hpp"
#include "library/FileRecord.hpp"
#include "TrackRequest.hpp"
#include "MediaInfo.hpp"

struct StreamInitiationResult
{
    // The stream's random ID (see StreamId) — the first segment of both
    // addresses, and of every subtitle track's ("<streamId>/<index>.vtt").
    std::string streamId;
    std::string streamAddress;
    std::optional<std::string> subtitleAddress;

    // True when this call launched a fresh ffmpeg conversion, false when it
    // reused the one already running (or already finished) for the same
    // file, tracks and target — a reused stream has segments ready to play.
    bool startedNewTranscode = false;

    // The tracks the stream plays, chosen or picked by TrackSelector: -1
    // for none (subtitles off).
    int audioTrackId = -1;
    int subtitleTrackId = -1;
};

class StreamPipeline
{
   public:
    // target is resolved once by the caller (see StreamTargets::Resolve);
    // every step of the start uses that copy. The error says why no stream
    // could be started.
    // request holds the tracks the user chose; TrackSelector picks the
    // others (see docs/scope/track-selection-scope.md).
    static std::expected<StreamInitiationResult, ConversionError> InitiateStream(
        const std::string& libraryId, const TrackRequest& request, const StreamTarget& target);

   private:
    struct StreamStart
    {
        std::string streamId;
        std::string streamAddress;
        bool startedNewTranscode = false;
    };

    [[nodiscard]] static TrackList SelectTracks(const FileRecord& fileRecord, const TrackRequest& request);

    // The track audioTrackId names — except an external audio file that's
    // no longer on disk, which falls back to a built-in track.
    [[nodiscard]] static std::optional<AudioTrack> SelectAudio(
        const FileRecord& fileRecord, std::optional<int> audioTrackId);

    // Writes the folder memory (see FolderTrackMemory::RememberChosen) for
    // the kinds the user chose in request, and only those: a track the
    // engine picked itself writes nothing. Local and remote streams alike.
    static void RememberFolderChoice(
        const FileRecord& fileRecord, const TrackRequest& request, const TrackList& trackList);

    // Whether the running (or finished) conversion already is what this
    // request asks for: same file, audio and video tracks, and target.
    [[nodiscard]] static bool CanReuseCurrentStream(
        const FileRecord& fileRecord, const TrackList& trackList, const StreamTarget& target);

    // Reuses the current stream when it matches, otherwise stops it and
    // starts a new one.
    [[nodiscard]] static std::expected<StreamStart, ConversionError> StartOrReuse(
        const FileRecord& fileRecord, const TrackList& trackList, const StreamTarget& target);

    // Gets the file's WebVTT sidecars onto disk (see SubtitleExtractor) and
    // returns the picked track's address, or std::nullopt when none was
    // picked or it couldn't be extracted (e.g. a bitmap PGS track).
    static std::optional<std::string> PrepareSidecarSubtitles(const FileRecord& fileRecord,
        const std::string& streamId, const std::optional<SubtitleTrack>& subtitleTrack);

    // The reused or started stream: its ID and entry file's address.
    [[nodiscard]] static StreamStart CurrentStreamStart(const StreamProfile& profile, bool startedNewTranscode);
};
