#include "EngineStreams.hpp"

#include "log/Log.hpp"
#include "pipeline/library/Library.hpp"
#include "pipeline/StreamId.hpp"
#include "pipeline/stream/StreamManager.hpp"
#include "pipeline/StreamPipeline.hpp"
#include "pipeline/stream/subtitles/SubtitleExtractor.hpp"

namespace mango::EngineStreams
{

void Start(StreamInitiation& payload, const std::string& fastHash, const TrackRequest& tracks,
    const StreamTarget& target)
{
    try
    {
        // Throws std::runtime_error for a fastHash not present in the
        // library — a plain failure result from this API's point of view,
        // not something that should cross into the ObjC/Swift boundary as
        // an uncaught C++ exception.
        const auto result = StreamPipeline::InitiateStream(fastHash, tracks, target);
        if (result)
        {
            payload.success = true;
            payload.streamId = result->streamId;
            payload.streamAddress = result->streamAddress;
            payload.subtitleAddress = result->subtitleAddress.value_or("");
            payload.startedNewTranscode = result->startedNewTranscode;
            payload.audioTrackId = result->audioTrackId;
            payload.subtitleTrackId = result->subtitleTrackId;
        }
        else
        {
            payload.error = ConversionErrors::ToString(result.error());
        }
    }
    catch (const std::exception& e)
    {
        Log::Error(LogCategory::Stream, "initiate failed for {}: {}", fastHash, e.what());
        payload.error = ConversionErrors::ToString(ConversionError::Unknown);
    }
}

bool SubtitleTrackCanBeServed(const std::string& fastHash, int subtitleTrackIndex)
{
    const auto fileRecord = Library::GetInstance().GetCachedFile(fastHash);
    if (!fileRecord)
    {
        return false;
    }

    // An external file is converted right away (well under a second): a
    // broken one must not be declared at all, and only converting it tells.
    if (fileRecord->FindExternalSubtitle(subtitleTrackIndex))
    {
        return SubtitleExtractor::WaitUntilExtracted(*fileRecord, subtitleTrackIndex);
    }

    return SubtitleExtractor::CanExtract(*fileRecord, subtitleTrackIndex);
}

std::vector<int> ServableSubtitleTracks(const std::string& fastHash)
{
    const auto fileRecord = Library::GetInstance().GetCachedFile(fastHash);
    if (!fileRecord)
    {
        return {};
    }

    std::vector<int> indices;
    for (const auto& track : fileRecord->mediaInfo.subtitles)
    {
        indices.push_back(track.index);
    }
    for (const auto& subtitle : fileRecord->externalSubtitles)
    {
        indices.push_back(subtitle.index);
    }
    std::erase_if(indices, [&](int index) { return !SubtitleTrackCanBeServed(fastHash, index); });
    return indices;
}

bool HasCover(const std::string& fastHash)
{
    const auto fileRecord = Library::GetInstance().GetCachedFile(fastHash);
    return fileRecord && fileRecord->coverFileName;
}

std::string CoverUrl(const std::string& contentUrl, const std::string& fastHash)
{
    const auto streamId = StreamId::FromStreamUrl(contentUrl);
    const auto stream = streamId ? StreamManager::GetInstance().FindStream(*streamId) : std::nullopt;
    if (!stream || stream->fastHash != fastHash || !HasCover(fastHash))
    {
        return {};
    }

    return contentUrl.substr(0, contentUrl.find("/stream/")) + "/cover/" + *streamId;
}

}  // namespace mango::EngineStreams
