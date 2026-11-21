#include "PeerConnections.hpp"

#include <algorithm>

#include "../../log/Log.hpp"
#include "../signaling/SignalingMessages.hpp"

namespace mango::remote
{

namespace
{

// Public STUN only: no TURN in v1 (see signal/README.md).
const std::vector<std::string> kIceServers{
    "stun:stun.cloudflare.com:3478",
    "stun:stun.l.google.com:19302",
};

constexpr std::chrono::seconds kFirstBackoff{5};
constexpr std::chrono::seconds kMaxBackoff{300};

// Bounds m_signalTargets: a request ID is only useful until its error comes back.
constexpr std::size_t kMaxTrackedSignals = 64;

std::string NewSessionId() { return Crypto::Base64UrlEncode(Crypto::RandomBytes(12)); }

}  // namespace

PeerConnections::PeerConnections(Mode mode, SessionThread& thread, const DeviceIdentity& identity,
    const PeerStore& peers, SignalingClient& signaling, Callbacks callbacks)
    : m_mode(mode),
      m_thread(thread),
      m_peers(peers),
      m_signaling(signaling),
      m_callbacks(std::move(callbacks)),
      m_context{.thread = thread,
          .identity = identity,
          .appVersion = {},
          .iceServers = kIceServers,
          .sendSdp = [this](const SdpPayload& payload) { SendSdp(payload); },
          .onChanged = [this](const std::string& peerId) { OnLinkChanged(peerId); },
          .onMessage = [this](const std::string& peerId, const std::string& text) {
              if (m_callbacks.onMessage)
              {
                  m_callbacks.onMessage(peerId, text);
              }
          },
          .onBinary = [this](const std::string& peerId, std::string bytes) {
              if (m_callbacks.onBinary)
              {
                  m_callbacks.onBinary(peerId, std::move(bytes));
              }
          }}
{
}

PeerConnections::~PeerConnections() { CloseAll(); }

void PeerConnections::OnSignalingReady()
{
    if (m_mode != Mode::Offer)
    {
        return;
    }
    for (const auto& peer : m_peers.All())
    {
        Connect(peer.deviceId);
    }
}

bool PeerConnections::Connect(const std::string& peerId)
{
    const auto* peer = m_peers.Find(peerId);
    if (!peer || m_mode != Mode::Offer)
    {
        return false;
    }

    auto& retry = m_retries[peerId];
    retry.wanted = true;
    m_thread.Cancel(std::exchange(retry.timer, 0));

    const auto link = FindLink(peerId);
    if (link && !PeerLink::IsFinished(link->GetState()))
    {
        return true;
    }
    Offer(*peer);
    return true;
}

void PeerConnections::Disconnect(const std::string& peerId)
{
    auto& retry = m_retries[peerId];
    retry.wanted = false;
    retry.lastLink = "disconnected";
    m_thread.Cancel(std::exchange(retry.timer, 0));
    CloseLink(peerId);
    m_callbacks.onChanged();
}

void PeerConnections::Forget(const std::string& peerId)
{
    Disconnect(peerId);
    m_retries.erase(peerId);
}

void PeerConnections::CloseAll()
{
    for (auto& [peerId, retry] : m_retries)
    {
        m_thread.Cancel(std::exchange(retry.timer, 0));
        retry.wanted = false;
    }
    auto links = std::exchange(m_links, {});
    for (auto& [peerId, link] : links)
    {
        link->Close();
        NoteLinkDown(peerId);
    }
}

void PeerConnections::SetInboundAllowed(bool allowed)
{
    m_inboundAllowed = allowed;
    if (!allowed)
    {
        CloseInbound();
    }
}

void PeerConnections::CloseInbound()
{
    std::vector<std::string> inbound;
    for (const auto& [peerId, link] : m_links)
    {
        if (!link->IsOfferer())
        {
            inbound.push_back(peerId);
        }
    }
    for (const auto& peerId : inbound)
    {
        Log::Info(LogCategory::Remote, "closing {}'s link: remote streaming isn't allowed", peerId.substr(0, 8));
        Disconnect(peerId);
    }
}

void PeerConnections::HandleSdpSignal(const std::string& from, const std::string& payloadText)
{
    const auto* peer = m_peers.Find(from);
    const auto payload = signaling::Read<SdpPayload>(payloadText);
    if (!peer || !payload)
    {
        return;  // Not paired (or revoked): ignored.
    }

    if (payload->from != from ||
        !SdpEnvelope::Verify(*payload, peer->publicKey, m_context.identity.DeviceId(), SdpEnvelope::NowUnixSeconds()))
    {
        Log::Warning(LogCategory::Remote, "ignored an {} with a bad signature from {}", payload->kind, peer->name);
        return;
    }

    // Only a client offers, and only to a host.
    if (payload->kind != (m_mode == Mode::Answer ? "offer" : "answer"))
    {
        Log::Debug(LogCategory::Remote, "ignored an {} from {}: wrong side", payload->kind, peer->name);
        return;
    }

    if (payload->kind == "offer")
    {
        if (!m_inboundAllowed)
        {
            Log::Info(LogCategory::Remote, "ignored {}'s offer: remote streaming isn't allowed", peer->name);
            return;
        }
        if (IsNewSession(payload->sessionId))
        {
            Answer(*peer, *payload);
        }
        return;
    }

    if (const auto link = FindLink(from); link && link->SessionId() == payload->sessionId)
    {
        link->HandleSdp(*payload);
    }
}

void PeerConnections::HandleSignalError(const std::string& requestId, const std::string& code)
{
    const auto target = m_signalTargets.find(requestId);
    if (target == m_signalTargets.end())
    {
        return;
    }
    const auto peerId = target->second;
    m_signalTargets.erase(target);

    if (code != "offline")
    {
        Log::Warning(LogCategory::Remote, "signal to {} failed: {}", peerId.substr(0, 8), code);
    }

    auto& retry = m_retries[peerId];
    retry.lastLink = code == "offline" ? "offline" : "failed";
    CloseLink(peerId);
    ScheduleRetry(peerId);
    m_callbacks.onChanged();
}

bool PeerConnections::Ping(const std::string& peerId)
{
    const auto link = FindLink(peerId);
    if (!link || link->GetState() != PeerLink::State::Ready)
    {
        return false;
    }
    link->Ping();
    return true;
}

bool PeerConnections::Send(const std::string& peerId, const std::string& text)
{
    const auto link = FindLink(peerId);
    return link && link->Send(text);
}

std::optional<std::string> PeerConnections::HelloExample(const std::string& peerId) const
{
    const auto link = FindLink(peerId);
    if (!link || link->GetState() != PeerLink::State::Ready || !link->PeerHelloMessage())
    {
        return std::nullopt;
    }
    return link->PeerHelloMessage()->initiateExample.str;
}

PeerLinkStatus PeerConnections::Status(const std::string& peerId) const
{
    PeerLinkStatus status;
    if (const auto retry = m_retries.find(peerId); retry != m_retries.end())
    {
        status.link = retry->second.lastLink;
    }

    if (const auto link = FindLink(peerId))
    {
        if (!PeerLink::IsFinished(link->GetState()))
        {
            status.link = PeerLink::ToString(link->GetState());
        }
        if (const auto& hello = link->PeerHelloMessage())
        {
            status.peerAppVersion = hello->appVersion;
        }
        if (const auto ping = link->LastPing())
        {
            status.pingMs = ping->count();
        }
    }
    return status;
}

std::vector<RemotePeerState> PeerConnections::PeerStates() const
{
    std::vector<RemotePeerState> states;
    for (const auto& peer : m_peers.All())
    {
        const auto status = Status(peer.deviceId);
        states.push_back({.deviceId = peer.deviceId,
            .name = peer.name,
            .pairedAtUnixSeconds = peer.pairedAtUnixSeconds,
            .link = status.link,
            .peerAppVersion = status.peerAppVersion,
            .pingMs = status.pingMs});
    }
    return states;
}

void PeerConnections::Offer(const PairedPeer& peer)
{
    CloseLink(peer.deviceId);
    auto link = PeerLink::Create(m_context, peer, true, NewSessionId());
    m_links[peer.deviceId] = link;
    link->Start();
    m_callbacks.onChanged();
}

void PeerConnections::Answer(const PairedPeer& peer, const SdpPayload& offer)
{
    // A new offer replaces whatever link there was: the peer restarted.
    CloseLink(peer.deviceId);
    auto link = PeerLink::Create(m_context, peer, false, offer.sessionId);
    m_links[peer.deviceId] = link;
    link->Start();
    link->HandleSdp(offer);
    m_callbacks.onChanged();
}

void PeerConnections::SendSdp(const SdpPayload& payload)
{
    const auto requestId = m_signaling.NextRequestId();
    const signaling::SignalOut signal{
        .to = payload.to, .payload = {signaling::Write(payload)}, .requestId = requestId};
    if (!m_signaling.Send(signaling::Write(signal)))
    {
        Log::Warning(LogCategory::Remote, "can't send the {}: signaling isn't connected", payload.kind);
        return;
    }

    if (m_signalTargets.size() >= kMaxTrackedSignals)
    {
        m_signalTargets.erase(m_signalTargets.begin());
    }
    m_signalTargets[requestId] = payload.to;
}

void PeerConnections::OnLinkChanged(const std::string& peerId)
{
    const auto link = FindLink(peerId);
    if (!link)
    {
        return;
    }

    const auto state = link->GetState();
    auto& retry = m_retries[peerId];
    if (state == PeerLink::State::Ready)
    {
        retry.backoff = kFirstBackoff;
        retry.failureNoticed = false;
        retry.lastLink = "disconnected";
        if (m_up.insert(peerId).second && m_callbacks.onLinkUp)
        {
            m_callbacks.onLinkUp(peerId, link->Transport());
        }
    }
    else if (PeerLink::IsFinished(state))
    {
        NoteLinkDown(peerId);
        // Closing a link may call back into here: handle it after.
        m_thread.Post([this, peerId, state] { OnLinkFinished(peerId, state); });
    }
    m_callbacks.onChanged();
}

void PeerConnections::OnLinkFinished(const std::string& peerId, PeerLink::State state)
{
    const auto found = m_retries.find(peerId);
    if (found == m_retries.end())
    {
        return;  // Forgotten meanwhile.
    }
    auto& retry = found->second;
    retry.lastLink = PeerLink::ToString(state);

    if (state == PeerLink::State::Failed)
    {
        NoteFailure(peerId, retry);
    }
    else if (state == PeerLink::State::Closed && retry.wanted)
    {
        m_callbacks.onNotice("peerLinkLost", peerId);
    }
    else if (state == PeerLink::State::PeerNeedsUpdate || state == PeerLink::State::UpdateRequired)
    {
        // Checked again at each reconnect, in case one side updated.
        retry.backoff = kMaxBackoff;
    }

    ScheduleRetry(peerId);
    m_callbacks.onChanged();
}

// Shown once per run of failures, not at every retry.
void PeerConnections::NoteFailure(const std::string& peerId, Retry& retry)
{
    if (!retry.failureNoticed)
    {
        retry.failureNoticed = true;
        m_callbacks.onNotice("peerConnectionFailed", peerId);
    }
}

void PeerConnections::ScheduleRetry(const std::string& peerId)
{
    auto& retry = m_retries[peerId];
    if (!retry.wanted || retry.timer != 0)
    {
        return;
    }

    Log::Info(LogCategory::Remote, "retrying {} in {} s", peerId.substr(0, 8), retry.backoff.count());
    retry.timer = m_thread.PostAfter(retry.backoff, [this, peerId] {
        auto& current = m_retries[peerId];
        current.timer = 0;
        if (current.wanted && m_signaling.GetState() == SignalingClient::State::Ready)
        {
            Connect(peerId);
        }
    });
    retry.backoff = std::min(retry.backoff * 2, kMaxBackoff);
}

// Taken out of m_links first: a link closed on purpose isn't reported as
// lost or retried.
void PeerConnections::CloseLink(const std::string& peerId)
{
    const auto found = m_links.find(peerId);
    if (found == m_links.end())
    {
        return;
    }
    const auto link = found->second;
    m_links.erase(found);
    link->Close();
    NoteLinkDown(peerId);
}

void PeerConnections::NoteLinkDown(const std::string& peerId)
{
    if (m_up.erase(peerId) > 0 && m_callbacks.onLinkDown)
    {
        m_callbacks.onLinkDown(peerId);
    }
}

std::shared_ptr<PeerLink> PeerConnections::FindLink(const std::string& peerId) const
{
    const auto found = m_links.find(peerId);
    return found != m_links.end() ? found->second : nullptr;
}

bool PeerConnections::IsNewSession(const std::string& sessionId)
{
    return m_seenSessions.insert(sessionId).second;
}

}  // namespace mango::remote
