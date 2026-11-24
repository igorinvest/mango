#include "EngineRemoteHost.hpp"

#include <algorithm>
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

namespace
{

// The subfolder's own name, or the added folder's for its root.
std::string FolderName(const LibraryEntryLocation& location, const std::vector<LibraryFolderEntry>& folders)
{
    if (!location.folderPath.empty())
    {
        const auto slash = location.folderPath.rfind('/');
        return slash == std::string::npos ? location.folderPath : location.folderPath.substr(slash + 1);
    }
    const auto folder = std::ranges::find(folders, location.folderId, &LibraryFolderEntry::folderId);
    return folder != folders.end() ? folder->name : std::string();
}

std::vector<std::string> FolderNames(const LibraryEntry& entry, const std::vector<LibraryFolderEntry>& folders)
{
    std::vector<std::string> names;
    for (const auto& location : entry.folderLocations)
    {
        auto name = FolderName(location, folders);
        if (!name.empty() && !std::ranges::contains(names, name))
        {
            names.push_back(std::move(name));
        }
    }
    return names;
}

}  // namespace

EngineRemoteHost::EngineRemoteHost(const Engine& engine, StreamingSession& session, CurrentConversion& conversion)
    : m_engine(engine), m_session(session), m_conversion(conversion)
{
}

// Where the files live on this computer is none of the peer's business:
// only the names of the folders holding them go.
std::vector<LibraryEntry> EngineRemoteHost::SharedLibrary()
{
    constexpr glz::opts lenient{.error_on_unknown_keys = false};
    std::vector<LibraryEntry> entries;
    std::vector<LibraryFolderEntry> folders;
    if (glz::read<lenient>(entries, m_engine.GetLibraryEntriesJson()) ||
        glz::read<lenient>(folders, m_engine.GetLibraryFoldersJson()))
    {
        return {};
    }
    for (auto& entry : entries)
    {
        entry.folderNames = FolderNames(entry, folders);
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
    // This computer's own UI shows the conversion it runs for the peer.
    m_conversion.StartedLocal();
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

TranscodeStatus EngineRemoteHost::CurrentTranscode() { return CurrentConversion::LocalStatus(); }

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
