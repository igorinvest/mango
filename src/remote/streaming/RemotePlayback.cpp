#include "RemotePlayback.hpp"

#include "../../log/Log.hpp"
#include "../../pipeline/stream/target/StreamTarget.hpp"
#include "../signaling/SignalingMessages.hpp"
#include "RemoteEntryId.hpp"
#include "RemoteTarget.hpp"

namespace mango::remote
{

namespace
{

InitiateResult Failed(const std::string& error)
{
    InitiateResult result;
    result.error = error;
    return result;
}

std::string Join(const std::vector<std::string>& items)
{
    std::string text;
    for (const auto& item : items)
    {
        text += (text.empty() ? "" : ", ") + item;
    }
    return text;
}

}  // namespace

RemotePlayback::RemotePlayback(SessionThread& thread, Callbacks callbacks)
    : m_thread(thread), m_callbacks(std::move(callbacks))
{
}

RemotePlayback::~RemotePlayback() { FailAll(); }

InitiateResult RemotePlayback::Initiate(
    const std::string& entryId, const TrackRequest& tracks, PlaylistType playlist, const std::string& tvDeviceKey)
{
    const auto parts = RemoteEntryId::Parse(entryId);
    if (!parts)
    {
        return Failed("unknown");
    }

    auto promise = std::make_shared<std::promise<InitiateResult>>();
    auto future = promise->get_future();
    InitiateRequest request;
    request.fastHash = parts->fastHash;
    request.audioTrackId = tracks.audioTrackId;
    request.subtitleTrackId = tracks.subtitleTrackId;
    request.tvDeviceKey = tvDeviceKey;
    m_thread.Post([this, alive = std::weak_ptr(m_lifetime), entryId, request, playlist, tvDeviceKey,
                      pending = Pending{parts->peerId, entryId, promise}] {
        if (alive.lock())
        {
            Send(entryId, pending, request, playlist, tvDeviceKey);
        }
    });

    if (future.wait_for(kInitiateTimeout) != std::future_status::ready)
    {
        Log::Warning(LogCategory::Remote, "{}: no answer to initiate in {} s", entryId, kInitiateTimeout.count());
        return Failed("peerTimeout");
    }
    return future.get();
}

// B's target for the TV, as the peer's example says it takes it.
void RemotePlayback::Send(const std::string& entryId, const Pending& pending, InitiateRequest request, PlaylistType playlist,
    const std::string& tvDeviceKey)
{
    const auto example = m_callbacks.example(pending.peerId);
    if (!example)
    {
        pending.result->set_value(Failed("peerOffline"));
        return;
    }

    const auto target = StreamTargets::Resolve(playlist, tvDeviceKey);
    const auto fitted =
        RemoteTarget::FitToExample(RemoteTarget::Describe(target, StreamProfiles::GetEncodingDefaults()), *example);
    if (!fitted.notApplied.empty())
    {
        Log::Info(LogCategory::Remote, "{}: not applied: {}", entryId, Join(fitted.notApplied));
        if (m_warned.insert(pending.peerId).second)
        {
            m_callbacks.onNotApplied(pending.peerId, fitted.notApplied);
        }
    }

    request.requestId = m_nextId++;
    request.target.str = fitted.json;
    m_pending.emplace(request.requestId, pending);
    if (!m_callbacks.send(pending.peerId, signaling::Write(request)))
    {
        Fail(request.requestId, "peerOffline");
    }
}

void RemotePlayback::HandleResult(const std::string& peerId, const std::string& text)
{
    const auto result = signaling::Read<InitiateResult>(text);
    const auto found = result ? m_pending.find(result->requestId) : m_pending.end();
    if (found == m_pending.end() || found->second.peerId != peerId)
    {
        return;
    }

    const auto pending = found->second;
    m_pending.erase(found);
    if (result->success)
    {
        Remember(pending.entryId, peerId, *result);
    }
    pending.result->set_value(*result);
}

void RemotePlayback::OnLinkUp(const std::string& peerId) { m_warned.erase(peerId); }

void RemotePlayback::OnLinkDown(const std::string& peerId)
{
    m_warned.erase(peerId);
    std::vector<std::uint32_t> requestIds;
    for (const auto& [requestId, pending] : m_pending)
    {
        if (pending.peerId == peerId)
        {
            requestIds.push_back(requestId);
        }
    }
    for (const auto requestId : requestIds)
    {
        Fail(requestId, "peerLinkLost");
    }
}

void RemotePlayback::FailAll()
{
    auto pending = std::exchange(m_pending, {});
    for (auto& [requestId, entry] : pending)
    {
        entry.result->set_value(Failed("peerLinkLost"));
    }
}

std::optional<RemotePlayback::Stream> RemotePlayback::Find(const std::string& entryId) const
{
    std::lock_guard lock(m_mutex);
    const auto found = m_streams.find(entryId);
    return found != m_streams.end() ? std::optional(found->second) : std::nullopt;
}

std::optional<std::string> RemotePlayback::FindByStream(const std::string& peerId, const std::string& streamId) const
{
    std::lock_guard lock(m_mutex);
    for (const auto& [entryId, stream] : m_streams)
    {
        if (stream.peerId == peerId && stream.streamId == streamId)
        {
            return entryId;
        }
    }
    return std::nullopt;
}

void RemotePlayback::Started(const std::string& entryId, double startSeconds)
{
    std::string peerId;
    std::string fastHash;
    {
        std::lock_guard lock(m_mutex);
        const auto found = m_streams.find(entryId);
        if (found == m_streams.end())
        {
            return;
        }
        found->second.lastPosition.reset();
        found->second.stoppedByUser = false;
        found->second.endSent = false;
        peerId = found->second.peerId;
        fastHash = found->second.fastHash;
    }
    SendToPeer(peerId, signaling::Write(PlaybackStart{.fastHash = fastHash, .startSeconds = startSeconds}));
}

void RemotePlayback::ReportProgress(
    const std::string& entryId, double positionSeconds, int audioTrackId, int subtitleTrackId)
{
    std::string peerId;
    std::string fastHash;
    {
        std::lock_guard lock(m_mutex);
        const auto found = m_streams.find(entryId);
        if (found == m_streams.end())
        {
            return;
        }
        found->second.lastPosition = positionSeconds;
        peerId = found->second.peerId;
        fastHash = found->second.fastHash;
    }
    SendToPeer(peerId, signaling::Write(PlaybackProgress{.fastHash = fastHash,
                           .positionSeconds = positionSeconds,
                           .audioTrackId = audioTrackId,
                           .subtitleTrackId = subtitleTrackId}));
}

void RemotePlayback::NoteStoppedByUser(const std::string& entryId)
{
    std::lock_guard lock(m_mutex);
    if (const auto found = m_streams.find(entryId); found != m_streams.end())
    {
        found->second.stoppedByUser = true;
    }
}

void RemotePlayback::Ended(const std::string& entryId)
{
    std::string peerId;
    std::string fastHash;
    {
        std::lock_guard lock(m_mutex);
        const auto found = m_streams.find(entryId);
        if (found == m_streams.end() || std::exchange(found->second.endSent, true))
        {
            return;
        }
        peerId = found->second.peerId;
        fastHash = found->second.fastHash;
    }
    const auto text = signaling::Write(PlaybackEnded{.fastHash = fastHash});
    m_thread.Post([this, alive = std::weak_ptr(m_lifetime), peerId, text] {
        if (alive.lock() && m_callbacks.send(peerId, text) && m_callbacks.onEnded)
        {
            m_callbacks.onEnded(peerId);
        }
    });
}

void RemotePlayback::Fail(std::uint32_t requestId, const std::string& error)
{
    const auto found = m_pending.find(requestId);
    if (found == m_pending.end())
    {
        return;
    }
    const auto pending = found->second;
    m_pending.erase(found);
    pending.result->set_value(Failed(error));
}

void RemotePlayback::Remember(const std::string& entryId, const std::string& peerId, const InitiateResult& result)
{
    const auto parts = RemoteEntryId::Parse(entryId);
    std::lock_guard lock(m_mutex);
    m_streams[entryId] = Stream{.peerId = peerId,
        .fastHash = parts ? parts->fastHash : std::string(),
        .streamId = result.streamId,
        .subtitleTracks = result.subtitleTracks,
        .hasCover = result.hasCover,
        .lastPosition = std::nullopt,
        .stoppedByUser = false};
}

void RemotePlayback::SendToPeer(const std::string& peerId, std::string text)
{
    m_thread.Post([this, alive = std::weak_ptr(m_lifetime), peerId, text = std::move(text)] {
        if (alive.lock())
        {
            m_callbacks.send(peerId, text);
        }
    });
}

}  // namespace mango::remote
