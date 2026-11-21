#include "StreamPipeline.hpp"

#include <format>
#include <mutex>
#include <optional>
#include <stdexcept>

#include "../configuration/TrackPreferencesStore.hpp"
#include "../file/File.hpp"
#include "stream/subtitles/SubtitleExtractor.hpp"
#include "library/FileRecord.hpp"
#include "library/Library.hpp"
#include "StreamId.hpp"
#include "stream/StreamManager.hpp"
#include "playback/FolderTrackMemory.hpp"
#include "playback/TrackSelector.hpp"
#include "../log/Log.hpp"

namespace
{
// Either side's audio/video track can be std::nullopt (e.g. a file with no
// audio track), so indices are compared via this rather than by
// dereferencing the optionals.
template <typename Track>
std::optional<int> TrackIndex(const std::optional<Track>& track)
{
    return track ? std::optional(track->index) : std::nullopt;
}
}  // namespace

std::expected<StreamInitiationResult, ConversionError> StreamPipeline::InitiateStream(
    const std::string& libraryId, const TrackRequest& request, const StreamTarget& target)
{
    auto fileRecord = Library::GetInstance().GetCachedFile(libraryId);
    if (!fileRecord)
    {
        throw std::runtime_error(
            std::format("Invalid libraryId: {}. File not found in the library.", libraryId));
    }

    const auto trackList = SelectTracks(*fileRecord, request);
    RememberFolderChoice(*fileRecord, request, trackList);

    // Serializes the whole check/stop/start decision: StreamManager's
    // getters, StopCurrentStream and StartStream each lock it individually,
    // so without this, two overlapping InitiateStream calls (e.g. quickly
    // switching subtitle then audio track) can interleave and one's
    // StopCurrentStream can delete the stream folder out from under the
    // other's freshly-started ffmpeg.
    static std::mutex switchMutex;
    std::lock_guard switchLock(switchMutex);

    const auto start = StartOrReuse(*fileRecord, trackList, target);
    if (!start)
    {
        return std::unexpected(start.error());
    }

    // Extracted after the switch above (rather than before it) so it always
    // lands in the folder that's actually current — StopCurrentStream deletes
    // the whole stream folder, so extracting first let a restart wipe the
    // sidecar file right after writing it.
    std::optional<std::string> subtitleAddress;
    if (target.profile.sidecarSubtitles)
    {
        subtitleAddress = PrepareSidecarSubtitles(*fileRecord, start->streamId, trackList.subtitleTrack);
    }

    return StreamInitiationResult{.streamId = start->streamId,
        .streamAddress = start->streamAddress,
        .subtitleAddress = subtitleAddress,
        .startedNewTranscode = start->startedNewTranscode,
        .audioTrackId = TrackIndex(trackList.audioTrack).value_or(-1),
        .subtitleTrackId = TrackIndex(trackList.subtitleTrack).value_or(-1)};
}

TrackList StreamPipeline::SelectTracks(const FileRecord& fileRecord, const TrackRequest& request)
{
    const auto selection = TrackSelector::SelectTracks(fileRecord, request, TrackContext::For(fileRecord));

    TrackList trackList;
    trackList.videoTrack = fileRecord.find_video_by_index();
    trackList.audioTrack = SelectAudio(fileRecord, selection.audioTrackId);
    trackList.subtitleTrack = selection.subtitleTrackId ? fileRecord.find_subtitle_by_index(*selection.subtitleTrackId)
                                                        : std::nullopt;
    return trackList;
}

std::optional<AudioTrack> StreamPipeline::SelectAudio(
    const FileRecord& fileRecord, std::optional<int> audioTrackId)
{
    if (!audioTrackId)
    {
        return std::nullopt;
    }

    const auto* external = fileRecord.FindExternalAudio(*audioTrackId);
    if (!external || File::Exists(File::FromUtf8(external->path)))
    {
        return fileRecord.find_audio_by_index(*audioTrackId);
    }

    // Deleted or moved since the last Refresh: a missing dub mustn't block
    // playback, so a built-in track plays instead.
    Log::Warning(LogCategory::Stream, "external audio file is gone, using a built-in track: {}", external->path);
    const auto fallback =
        TrackSelector::SelectBuiltInAudioTrack(fileRecord.mediaInfo, TrackPreferencesStore::Get());
    return fallback ? fileRecord.find_audio_by_index(*fallback) : std::nullopt;
}

void StreamPipeline::RememberFolderChoice(
    const FileRecord& fileRecord, const TrackRequest& request, const TrackList& trackList)
{
    FolderTrackMemory::GetInstance().RememberChosen(fileRecord.filePath, request, trackList);
}

bool StreamPipeline::CanReuseCurrentStream(
    const FileRecord& fileRecord, const TrackList& trackList, const StreamTarget& target)
{
    auto& manager = StreamManager::GetInstance();
    const auto currentFile = manager.GetCurrentFileRecord();
    const auto currentTracks = manager.GetCurrentTrackList();

    // A failed conversion is restarted even for an identical request —
    // reusing it would hand out a stream that stops wherever ffmpeg died.
    // A changed preset, mode or TV profile restarts it too. Without sidecar
    // subtitles (HLS) the subtitle track is muxed into the stream, so a
    // different one needs a new conversion as well.
    return currentFile && currentTracks && !manager.HasCurrentTranscodeFailed() &&
           fileRecord.fastHash == currentFile->fastHash && manager.GetCurrentTarget() == target &&
           TrackIndex(currentTracks->audioTrack) == TrackIndex(trackList.audioTrack) &&
           TrackIndex(currentTracks->videoTrack) == TrackIndex(trackList.videoTrack) &&
           (target.profile.sidecarSubtitles ||
               TrackIndex(currentTracks->subtitleTrack) == TrackIndex(trackList.subtitleTrack));
}

std::expected<StreamPipeline::StreamStart, ConversionError> StreamPipeline::StartOrReuse(
    const FileRecord& fileRecord, const TrackList& trackList, const StreamTarget& target)
{
    if (CanReuseCurrentStream(fileRecord, trackList, target))
    {
        return CurrentStreamStart(target.profile, false);
    }

    auto& manager = StreamManager::GetInstance();
    if (manager.GetCurrentFileRecord())
    {
        manager.StopCurrentStream();
    }

    const auto started = manager.StartStream(fileRecord, trackList, target);
    if (!started)
    {
        return std::unexpected(started.error());
    }
    return CurrentStreamStart(target.profile, true);
}

StreamPipeline::StreamStart StreamPipeline::CurrentStreamStart(
    const StreamProfile& profile, bool startedNewTranscode)
{
    // Only read under InitiateStream's switchMutex, so nothing replaced the
    // stream since it was reused or started.
    const auto streamId = StreamManager::GetInstance().GetCurrentStreamId().value_or("");
    return StreamStart{.streamId = streamId,
        .streamAddress = StreamId::EntryPath(streamId, profile.defaultFileName + profile.defaultExtension),
        .startedNewTranscode = startedNewTranscode};
}

std::optional<std::string> StreamPipeline::PrepareSidecarSubtitles(const FileRecord& fileRecord,
    const std::string& streamId, const std::optional<SubtitleTrack>& subtitleTrack)
{
    // No subtitles asked for yet: extract them all in the background so
    // they're likely on disk by the time the user picks one, and the
    // switch can happen in place on the TV.
    if (!subtitleTrack)
    {
        SubtitleExtractor::ExtractAllInBackground(fileRecord);
        return std::nullopt;
    }

    const auto address = StreamId::EntryPath(streamId, std::to_string(subtitleTrack->index) + ".vtt");

    // A picked external file converts on its own in well under a second;
    // the built-in tracks follow in the background.
    if (fileRecord.FindExternalSubtitle(subtitleTrack->index))
    {
        SubtitleExtractor::ExtractAllInBackground(fileRecord);
        return SubtitleExtractor::WaitUntilExtracted(fileRecord, subtitleTrack->index)
            ? std::optional(address)
            : std::nullopt;
    }

    // The picked track is needed right now — and extracting every track
    // costs about the same single pass over the file as extracting one.
    SubtitleExtractor::ExtractAll(fileRecord);

    if (!SubtitleExtractor::IsExtracted(fileRecord, subtitleTrack->index))
    {
        return std::nullopt;
    }
    return address;
}
