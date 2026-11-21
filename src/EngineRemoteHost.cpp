#include "EngineRemoteHost.hpp"

#include <format>

#include <glaze/glaze.hpp>

#include "EngineStreams.hpp"
#include "log/Log.hpp"
#include "pipeline/library/Library.hpp"
#include "pipeline/playback/PlaybackEndDetector.hpp"
#include "pipeline/playback/PlaybackHistory.hpp"
#include "pipeline/stream/StreamManager.hpp"
#include "server/StreamingSession.hpp"

namespace mango
{

EngineRemoteHost::EngineRemoteHost(const Engine& engine, StreamingSession& session)
    : m_engine(engine), m_session(session)
{
}

// Where the files live on this computer is none of the peer's business.
std::vector<LibraryEntry> EngineRemoteHost::SharedLibrary()
{
    std::vector<LibraryEntry> entries;
    if (glz::read<glz::opts{.error_on_unknown_keys = false}>(entries, m_engine.GetLibraryEntriesJson()))
    {
        return {};
    }
    for (auto& entry : entries)
    {
        entry.filePath.clear();
        entry.coverPath.clear();
        entry.folderLocations.clear();
    }
    return entries;
}

std::optional<std::filesystem::path> EngineRemoteHost::CoverFile(const std::string& fastHash)
{
    const auto fileRecord = Library::GetInstance().GetCachedFile(fastHash);
    if (!fileRecord || !fileRecord->coverFileName)
    {
        return std::nullopt;
    }
    return fileRecord->getMetadataFolder() / *fileRecord->coverFileName;
}

remote::HostedStream EngineRemoteHost::StartStream(
    const std::string& fastHash, const TrackRequest& tracks, const StreamTarget& target)
{
    remote::HostedStream hosted;
    auto& payload = hosted.initiation;
    PlaybackEndDetector::GetInstance().BeginSession(fastHash);

    // The forwarding adapter fetches from this server, as a TV would.
    const auto serverUrl = m_session.Open();
    if (!serverUrl)
    {
        payload.error = "noNetwork";
        return hosted;
    }

    payload.serverUrl = *serverUrl;
    EngineStreams::Start(payload, fastHash, tracks, target);
    if (!payload.success)
    {
        Log::Error(LogCategory::Stream, "no remote stream started for {}: {}", fastHash, payload.error);
        m_session.NotePlaybackEnded();
        return hosted;
    }

    Log::Info(LogCategory::Stream, "{} conversion for a remote peer: {}",
        payload.startedNewTranscode ? "started a new" : "reused the running", payload.streamAddress);
    hosted.subtitleTracks = EngineStreams::ServableSubtitleTracks(fastHash);
    hosted.hasCover = EngineStreams::HasCover(fastHash);
    return hosted;
}

std::optional<std::string> EngineRemoteHost::ServerUrl()
{
    const auto status = m_session.GetStatus();
    if (!status.isListening)
    {
        return std::nullopt;
    }
    return std::format("http://{}:{}", status.ip, status.port);
}

std::string EngineRemoteHost::CurrentStreamFastHash()
{
    const auto current = StreamManager::GetInstance().GetCurrentFileRecord();
    return current ? current->fastHash : std::string();
}

std::string EngineRemoteHost::Title(const std::string& fastHash)
{
    const auto fileRecord = Library::GetInstance().GetCachedFile(fastHash);
    return fileRecord ? fileRecord->libraryName : fastHash;
}

void EngineRemoteHost::PlaybackStarted(const std::string& fastHash, double startSeconds)
{
    PlaybackHistory::GetInstance().BeginSession(fastHash, startSeconds);
}

void EngineRemoteHost::ReportPlaybackProgress(
    const std::string& fastHash, double positionSeconds, int audioTrackId, int subtitleTrackId)
{
    PlaybackHistory::GetInstance().Report(fastHash, positionSeconds, audioTrackId, subtitleTrackId);

    // Like a TV status poll here: a paused TV fetches nothing, but its
    // stream must stay reachable.
    if (CurrentStreamFastHash() == fastHash)
    {
        m_session.NoteStillPlaying();
    }
}

void EngineRemoteHost::PlaybackEnded(const std::string& fastHash)
{
    if (CurrentStreamFastHash() == fastHash)
    {
        Log::Info(LogCategory::Remote, "the peer's playback of {} ended", fastHash);
        m_session.NotePlaybackEnded();
    }
}

}  // namespace mango
