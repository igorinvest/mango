#include "EngineRemote.hpp"

#include <algorithm>

#include "log/Log.hpp"
#include "pipeline/StreamId.hpp"
#include "remote/RemoteCore.hpp"
#include "remote/streaming/RemoteEntryId.hpp"
#include "remote/RemoteStreaming.hpp"
#include "server/StreamingSession.hpp"

namespace mango
{

namespace
{

constexpr std::string_view kRouteMarker = "/remote/";
constexpr std::size_t kPeerIdLength = 64;

StreamInitiation FromResult(const remote::InitiateResult& result)
{
    StreamInitiation payload;
    payload.success = result.success;
    payload.error = result.success ? std::string() : (result.error.empty() ? "unknown" : result.error);
    payload.streamId = result.streamId;
    payload.streamAddress = result.streamAddress;
    payload.subtitleAddress = result.subtitleAddress;
    payload.startedNewTranscode = result.startedNewTranscode;
    payload.audioTrackId = result.audioTrackId;
    payload.subtitleTrackId = result.subtitleTrackId;
    return payload;
}

// The <peerId> of a ".../remote/<peerId>/..." URL.
std::optional<std::string> PeerOfUrl(std::string_view url)
{
    const auto start = url.find(kRouteMarker);
    if (start == std::string_view::npos)
    {
        return std::nullopt;
    }
    const auto peerId = url.substr(start + kRouteMarker.size(), kPeerIdLength);
    const auto after = start + kRouteMarker.size() + kPeerIdLength;
    if (peerId.size() != kPeerIdLength || after >= url.size() || url[after] != '/')
    {
        return std::nullopt;
    }
    return std::string(peerId);
}

}  // namespace

EngineRemote::EngineRemote(StreamingSession& session, RemoteStreaming& remote) : m_session(session), m_remote(remote)
{
}

bool EngineRemote::Is(const std::string& fastHash) { return remote::RemoteEntryId::Is(fastHash); }

StreamInitiation EngineRemote::Initiate(
    const std::string& entryId, const TrackRequest& tracks, PlaylistType playlist, const std::string& deviceKey)
{
    const auto parts = remote::RemoteEntryId::Parse(entryId);
    const auto serverUrl = parts ? m_session.Open() : std::nullopt;
    if (!serverUrl)
    {
        StreamInitiation failed;
        failed.error = parts ? "noNetwork" : "unknown";
        return failed;
    }

    auto payload =
        FromResult(m_remote.Core().Playback().Initiate(entryId, tracks, playlist, deviceKey));
    payload.serverUrl = *serverUrl + std::string(kRouteMarker) + parts->peerId;
    if (!payload.success)
    {
        m_session.NotePlaybackEnded();
    }
    return payload;
}

std::string EngineRemote::PrepareLoad(const std::string& entryId, const std::string& contentUrl,
    std::vector<CastSubtitleTrackRef>& tracks, int& activeTrack, double startSeconds)
{
    const auto stream = m_remote.Core().Playback().Find(entryId);
    const auto servable = [&](int index) { return stream && std::ranges::contains(stream->subtitleTracks, index); };

    std::erase_if(tracks, [&](const CastSubtitleTrackRef& track) { return !servable(track.index); });
    if (activeTrack >= 0 && !servable(activeTrack))
    {
        Log::Warning(LogCategory::Subtitles, "{} can't serve subtitle track {}; loading without subtitles", entryId,
            activeTrack);
        activeTrack = -1;
    }
    if (!stream)
    {
        return {};
    }

    m_remote.Core().Playback().Started(entryId, startSeconds);
    if (!stream->hasCover)
    {
        return {};
    }
    return contentUrl.substr(0, contentUrl.find("/stream/")) + "/cover/" + stream->streamId;
}

bool EngineRemote::CanServeSubtitle(const std::string& entryId, int subtitleTrackIndex) const
{
    const auto stream = m_remote.Core().Playback().Find(entryId);
    return stream && std::ranges::contains(stream->subtitleTracks, subtitleTrackIndex);
}

void EngineRemote::ReportProgress(
    const std::string& entryId, double positionSeconds, int audioTrackId, int subtitleTrackId)
{
    m_remote.Core().Playback().ReportProgress(entryId, positionSeconds, audioTrackId, subtitleTrackId);
}

PlaybackEndKind EngineRemote::Classify(const std::string& entryId, const PlayerEnd& end)
{
    const auto stream = m_remote.Core().Playback().Find(entryId);
    const auto entry = m_remote.Core().FindLibraryEntry(entryId);
    const auto kind = PlaybackEndDetector::Decide(end, entry ? entry->durationSeconds : 0.0,
        stream ? stream->lastPosition : std::nullopt, stream && stream->stoppedByUser);
    if (kind == PlaybackEndKind::Finished || kind == PlaybackEndKind::Stopped)
    {
        m_session.NotePlaybackEnded();
        m_remote.Core().Playback().Ended(entryId);
    }
    return kind;
}

void EngineRemote::NoteStoppedByUser(const std::string& entryId)
{
    m_remote.Core().Playback().NoteStoppedByUser(entryId);
    m_remote.Core().Playback().Ended(entryId);
    m_session.NotePlaybackEnded();
}

std::string EngineRemote::EntryForUrl(const std::string& contentUrl) const
{
    const auto peerId = PeerOfUrl(contentUrl);
    const auto streamId = peerId ? StreamId::FromStreamUrl(contentUrl) : std::nullopt;
    const auto entryId = streamId ? m_remote.Core().Playback().FindByStream(*peerId, *streamId) : std::nullopt;
    return entryId.value_or("");
}

}  // namespace mango
