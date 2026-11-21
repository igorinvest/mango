#include "RemoteSource.hpp"

#include <fstream>
#include <iterator>

#include "../../log/Log.hpp"
#include "../../server/RequestGuard.hpp"
#include "../signaling/SignalingMessages.hpp"
#include "RemoteTarget.hpp"

namespace mango::remote
{

namespace
{

constexpr std::string_view kLibraryPath = "/library";
constexpr std::string_view kCoverPrefix = "/library-cover/";

// Covers are a few hundred KB; anything far bigger isn't one.
constexpr std::uintmax_t kMaxCoverBytes = 16 * 1024 * 1024;

std::optional<std::string> ReadFile(const std::filesystem::path& path)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > kMaxCoverBytes)
    {
        return std::nullopt;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::string ShortId(const std::string& deviceId) { return deviceId.substr(0, 8); }

// A requested track for the log: "auto" when this side picks.
std::string Describe(std::optional<int> trackId) { return trackId ? std::to_string(*trackId) : "auto"; }

InitiateResult Unavailable(std::uint32_t requestId)
{
    InitiateResult result;
    result.requestId = requestId;
    result.error = "unavailable";
    return result;
}

}  // namespace

RemoteSource::RemoteSource(SessionThread& thread, Callbacks callbacks)
    : m_thread(thread), m_callbacks(std::move(callbacks))
{
}

RemoteSource::~RemoteSource() { m_worker.Stop(); }

void RemoteSource::SetHost(std::shared_ptr<RemoteHost> host)
{
    std::lock_guard lock(m_hostMutex);
    m_host = std::move(host);
}

std::shared_ptr<RemoteHost> RemoteSource::Host() const
{
    std::lock_guard lock(m_hostMutex);
    return m_host;
}

bool RemoteSource::HandleMessage(const std::string& peerId, const std::string& type, const std::string& text)
{
    const auto host = Host();
    if (type == "initiate")
    {
        if (const auto request = signaling::Read<InitiateRequest>(text))
        {
            Initiate(peerId, *request);
        }
        return true;
    }
    if (type == "playbackStart")
    {
        if (const auto start = signaling::Read<PlaybackStart>(text); start && host)
        {
            host->PlaybackStarted(start->fastHash, start->startSeconds);
        }
        return true;
    }
    if (type == "playbackEnded")
    {
        if (const auto ended = signaling::Read<PlaybackEnded>(text); ended && host)
        {
            host->PlaybackEnded(ended->fastHash);
        }
        return true;
    }
    if (type == "playbackProgress")
    {
        if (const auto progress = signaling::Read<PlaybackProgress>(text); progress && host)
        {
            host->ReportPlaybackProgress(
                progress->fastHash, progress->positionSeconds, progress->audioTrackId, progress->subtitleTrackId);
        }
        return true;
    }
    return false;
}

ForwardingAdapter::Host RemoteSource::AdapterHost()
{
    return {.serverUrl = [this]() -> std::optional<std::string> {
                const auto host = Host();
                return host ? host->ServerUrl() : std::nullopt;
            },
        .localRoute = [this](const std::string& path) { return LocalRoute(path); }};
}

void RemoteSource::Initiate(const std::string& peerId, const InitiateRequest& request)
{
    Log::Info(LogCategory::Remote, "{} asks for {} audio={} subtitle={} (TV {})", ShortId(peerId), request.fastHash,
        Describe(request.audioTrackId), Describe(request.subtitleTrackId),
        request.tvDeviceKey.empty() ? "-" : request.tvDeviceKey);

    m_worker.Run([this, peerId, request] {
        const auto host = Host();
        const auto previous = host ? host->CurrentStreamFastHash() : std::string();
        auto result = host ? Start(*host, request) : Unavailable(request.requestId);
        m_thread.Post([this, alive = std::weak_ptr(m_lifetime), peerId, fastHash = request.fastHash, previous,
                          result] {
            if (alive.lock())
            {
                OnStarted(peerId, fastHash, previous, result);
            }
        });
    });
}

InitiateResult RemoteSource::Start(RemoteHost& host, const InitiateRequest& request)
{
    InitiateResult result;
    result.requestId = request.requestId;
    const auto target = RemoteTarget::Read(request.target.str);
    if (!target)
    {
        Log::Warning(LogCategory::Remote, "initiate {} refused: target {}", request.fastHash, target.error());
        result.error = "invalidTarget";
        return result;
    }

    const auto hosted = host.StartStream(
        request.fastHash, TrackRequest{request.audioTrackId, request.subtitleTrackId}, *target);
    const auto& initiation = hosted.initiation;
    result.success = initiation.success;
    result.error = initiation.error;
    result.streamId = initiation.streamId;
    result.streamAddress = initiation.streamAddress;
    result.subtitleAddress = initiation.subtitleAddress;
    result.startedNewTranscode = initiation.startedNewTranscode;
    result.subtitleTracks = hosted.subtitleTracks;
    result.hasCover = hosted.hasCover;
    result.audioTrackId = initiation.audioTrackId;
    result.subtitleTrackId = initiation.subtitleTrackId;
    return result;
}

void RemoteSource::OnStarted(
    const std::string& peerId, const std::string& fastHash, const std::string& previous, const InitiateResult& result)
{
    m_callbacks.send(peerId, signaling::Write(result));
    const auto host = Host();
    if (!result.success || !host || previous == fastHash)
    {
        return;
    }

    if (!previous.empty() && !m_remoteFiles.contains(previous))
    {
        m_callbacks.onNotice("streamStoppedByPeer", peerId, host->Title(previous));
    }
    m_remoteFiles.insert(fastHash);
    m_callbacks.onNotice("remoteStreamStarted", peerId, host->Title(fastHash));
}

std::optional<LocalResponse> RemoteSource::LocalRoute(const std::string& path)
{
    const auto host = Host();
    if (!host)
    {
        return std::nullopt;
    }
    if (path == kLibraryPath)
    {
        return LibraryResponse(*host);
    }
    if (path.starts_with(kCoverPrefix))
    {
        const auto fastHash = path.substr(kCoverPrefix.size());
        if (const auto cover = CoverResponse(*host, fastHash))
        {
            return cover;
        }
        return LocalResponse{.status = 404, .contentType = "text/plain", .body = "no cover"};
    }
    return std::nullopt;
}

LocalResponse RemoteSource::LibraryResponse(RemoteHost& host)
{
    RemoteLibrary library{.entries = host.SharedLibrary(), .covers = {}};
    for (const auto& entry : library.entries)
    {
        if (host.CoverFile(entry.fastHash))
        {
            library.covers.push_back(entry.fastHash);
        }
    }
    return {.status = 200, .contentType = "application/json", .body = signaling::Write(library)};
}

std::optional<LocalResponse> RemoteSource::CoverResponse(RemoteHost& host, const std::string& fastHash)
{
    if (!RequestGuard::IsSafePathSegment(fastHash))
    {
        return std::nullopt;
    }
    const auto file = host.CoverFile(fastHash);
    auto bytes = file ? ReadFile(*file) : std::nullopt;
    if (!bytes)
    {
        return std::nullopt;
    }
    const auto type = file->extension() == ".png" ? "image/png" : "image/jpeg";
    return LocalResponse{.status = 200, .contentType = type, .body = std::move(*bytes)};
}

}  // namespace mango::remote
