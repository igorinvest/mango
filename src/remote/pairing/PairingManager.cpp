#include "PairingManager.hpp"

#include <map>

#include "../../log/Log.hpp"
#include "../signaling/SdpEnvelope.hpp"

namespace mango::remote
{

namespace
{

// How long an invite lasts; the service's maximum.
constexpr std::chrono::seconds kInviteLifetime{600};

// From joining to the service's answer.
constexpr std::chrono::seconds kJoinTimeout{30};

// From the introduction to both proofs checked.
constexpr std::chrono::seconds kConfirmTimeout{30};

std::string FailureReason(const std::string& code)
{
    static const std::map<std::string, std::string> kReasons{
        {"offline", "offline"},
        {"invite_not_found", "notFound"},
        {"invite_limit", "limit"},
    };
    const auto found = kReasons.find(code);
    return found != kReasons.end() ? found->second : code;
}

}  // namespace

PairingManager::PairingManager(Role role, SessionThread& thread, const DeviceIdentity& identity, PeerStore& peers,
    SignalingClient& signaling, Callbacks callbacks)
    : m_role(role),
      m_thread(thread),
      m_identity(identity),
      m_peers(peers),
      m_signaling(signaling),
      m_callbacks(std::move(callbacks))
{
}

PairingManager::~PairingManager() { m_thread.Cancel(std::exchange(m_timer, 0)); }

void PairingManager::OpenInvite()
{
    if (m_role != Role::Inviter)
    {
        return;
    }
    Cancel();
    const auto secret = PairingProof::NewInviteSecret();
    m_pending = {};
    m_pending.side = Side::Inviter;
    m_pending.secret = secret;
    m_pending.inviteHash = PairingProof::InviteHash(secret);
    SetStage("opening");
    StartTimer(kInviteLifetime, "expired");

    if (m_signaling.GetState() == SignalingClient::State::Ready)
    {
        SendInviteOpen();
    }
}

bool PairingManager::Join(const std::string& link)
{
    const auto invite = PairingProof::ParseLink(link);
    if (!invite || m_role != Role::Joiner)
    {
        return false;
    }
    if (invite->deviceId == m_identity.DeviceId())
    {
        Fail("ownInvite");
        return true;
    }

    Cancel();
    m_pending = {};
    m_pending.side = Side::Joiner;
    m_pending.secret = invite->secret;
    m_pending.inviteHash = PairingProof::InviteHash(invite->secret);
    m_pending.expectedDeviceId = invite->deviceId;
    SetStage("joining");
    StartTimer(kJoinTimeout, "timeout");

    if (m_signaling.GetState() == SignalingClient::State::Ready)
    {
        SendInviteJoin();
    }
    return true;
}

void PairingManager::Cancel()
{
    if (m_pending.side == Side::Inviter && !m_pending.requestId.empty())
    {
        m_signaling.Send(signaling::Write(signaling::InviteClose{.inviteHash = m_pending.inviteHash}));
    }
    if (m_pending.side != Side::None)
    {
        Log::Info(LogCategory::Remote, "pairing cancelled");
    }
    Reset();
}

void PairingManager::OnSignalingReady()
{
    if (m_pending.side == Side::Inviter && m_pending.requestId.empty())
    {
        SendInviteOpen();
    }
    else if (m_pending.side == Side::Joiner && m_pending.requestId.empty())
    {
        SendInviteJoin();
    }
}

void PairingManager::OnSignalingLost()
{
    // Not sent yet: it still goes out once the service is back.
    if (m_pending.side != Side::None && !m_pending.requestId.empty())
    {
        Fail("signaling");
    }
}

bool PairingManager::HandleMessage(const std::string& type, const std::string& text)
{
    if (type == "invite.opened")
    {
        HandleInviteOpened(text);
    }
    else if (type == "invite.joined")
    {
        HandleInviteJoined(text);
    }
    else if (type == "invite.accepted")
    {
        HandleInviteAccepted(text);
    }
    else if (type == "error")
    {
        const auto error = signaling::Read<signaling::Error>(text);
        if (!error || m_pending.requestId.empty() || signaling::RequestIdOf(*error) != m_pending.requestId)
        {
            return false;
        }
        Fail(FailureReason(error->code));
    }
    else
    {
        return false;
    }
    return true;
}

void PairingManager::HandlePairSignal(const std::string& from, const std::string& payloadText)
{
    if (m_pending.side == Side::None)
    {
        return;
    }

    // The service sends invite.accepted before relaying A's proof, but keep
    // a proof that overtakes it anyway.
    if (!m_pending.peer)
    {
        if (m_pending.side == Side::Joiner && from == m_pending.expectedDeviceId)
        {
            m_pending.earlyProofPayload = payloadText;
        }
        return;
    }

    if (from != m_pending.peer->deviceId)
    {
        return;
    }
    if (const auto payload = signaling::Read<PairPayload>(payloadText))
    {
        CheckProof(*payload);
    }
}

void PairingManager::SendInviteOpen()
{
    m_pending.requestId = m_signaling.NextRequestId();
    const signaling::InviteOpen open{.inviteHash = m_pending.inviteHash,
        .ttl = static_cast<int>(kInviteLifetime.count()),
        .requestId = m_pending.requestId};
    m_signaling.Send(signaling::Write(open));
}

void PairingManager::SendInviteJoin()
{
    m_pending.requestId = m_signaling.NextRequestId();
    m_signaling.Send(signaling::Write(signaling::InviteJoin{.inviteSecret = m_pending.secret, .requestId = m_pending.requestId}));
}

void PairingManager::HandleInviteOpened(const std::string& text)
{
    const auto opened = signaling::Read<signaling::InviteOpened>(text);
    if (!opened || m_pending.side != Side::Inviter || opened->inviteHash != m_pending.inviteHash)
    {
        return;
    }

    m_status.link = PairingProof::FormatLink({m_pending.secret, m_identity.DeviceId()});
    m_status.expiresAtUnixSeconds = SdpEnvelope::NowUnixSeconds() + kInviteLifetime.count();
    Log::Info(LogCategory::Remote, "invite open, waiting for the other device");
    SetStage("waitingForPeer");
}

void PairingManager::HandleInviteJoined(const std::string& text)
{
    const auto joined = signaling::Read<signaling::InvitePeer>(text);
    if (joined && m_pending.side == Side::Inviter && joined->inviteHash == m_pending.inviteHash && !m_pending.peer)
    {
        Introduce(joined->peer);
    }
}

void PairingManager::HandleInviteAccepted(const std::string& text)
{
    const auto accepted = signaling::Read<signaling::InvitePeer>(text);
    if (!accepted || m_pending.side != Side::Joiner || accepted->inviteHash != m_pending.inviteHash || m_pending.peer)
    {
        return;
    }

    // The link names A: the service must have introduced A, not another.
    if (accepted->peer.deviceId != m_pending.expectedDeviceId)
    {
        Log::Warning(LogCategory::Remote, "the service introduced {}, the link names {}",
            accepted->peer.deviceId, m_pending.expectedDeviceId);
        Fail("wrongPeer");
        return;
    }
    Introduce(accepted->peer);
}

void PairingManager::Introduce(const signaling::PeerKey& peer)
{
    if (DeviceIdentity::DeviceIdOf(peer.publicKey) != peer.deviceId || peer.deviceId == m_identity.DeviceId())
    {
        Fail("wrongPeer");
        return;
    }

    m_pending.peer = peer;
    m_pending.requestId.clear();
    SetStage("confirming");
    StartTimer(kConfirmTimeout, "timeout");
    SendProof();

    if (auto early = std::exchange(m_pending.earlyProofPayload, std::nullopt))
    {
        HandlePairSignal(peer.deviceId, *early);
    }
}

void PairingManager::SendProof()
{
    const auto side = m_pending.side == Side::Inviter ? PairingProof::Side::Inviter : PairingProof::Side::Joiner;
    const PairPayload payload{.proof = PairingProof::Compute(m_pending.secret, side, InviterKey(), JoinerKey()),
        .name = m_deviceName,
        .side = m_pending.side == Side::Inviter ? "inviter" : "joiner"};

    m_pending.requestId = m_signaling.NextRequestId();
    const signaling::SignalOut signal{
        .to = m_pending.peer->deviceId, .payload = {signaling::Write(payload)}, .requestId = m_pending.requestId};
    m_pending.sentProof = m_signaling.Send(signaling::Write(signal));
}

void PairingManager::CheckProof(const PairPayload& payload)
{
    // The proof was made by the other side.
    const auto sender = m_pending.side == Side::Inviter ? PairingProof::Side::Joiner : PairingProof::Side::Inviter;
    if (!PairingProof::Verify(payload.proof, m_pending.secret, sender, InviterKey(), JoinerKey()))
    {
        Log::Warning(LogCategory::Remote, "pairing proof from {} doesn't match", m_pending.peer->deviceId);
        Fail("badProof");
        return;
    }
    Finish(payload.name);
}

void PairingManager::Finish(const std::string& peerName)
{
    PairedPeer peer{.deviceId = m_pending.peer->deviceId,
        .publicKey = m_pending.peer->publicKey,
        .name = peerName.empty() ? "Mango" : peerName,
        .pairedAtUnixSeconds = SdpEnvelope::NowUnixSeconds()};

    Log::Info(LogCategory::Remote, "paired with {} ({}) as {}", peer.name, peer.deviceId,
        m_role == Role::Inviter ? "a client" : "a source");
    m_peers.Add(peer);
    Reset();
    m_callbacks.onPaired(peer);
}

void PairingManager::Fail(const std::string& reason)
{
    Log::Warning(LogCategory::Remote, "pairing failed: {}", reason);
    if (m_pending.side == Side::Inviter && !m_pending.requestId.empty() && !m_pending.peer)
    {
        m_signaling.Send(signaling::Write(signaling::InviteClose{.inviteHash = m_pending.inviteHash}));
    }
    Reset();
    m_callbacks.onFailed(reason);
}

void PairingManager::Reset()
{
    m_thread.Cancel(std::exchange(m_timer, 0));
    m_pending = {};
    m_status.link.clear();
    m_status.expiresAtUnixSeconds = 0;
    SetStage("idle");
}

void PairingManager::SetStage(std::string stage)
{
    m_status.stage = std::move(stage);
    m_callbacks.onChanged();
}

void PairingManager::StartTimer(std::chrono::seconds timeout, std::string reason)
{
    m_thread.Cancel(std::exchange(m_timer, 0));
    m_timer = m_thread.PostAfter(timeout, [this, reason = std::move(reason)] {
        m_timer = 0;
        Fail(reason);
    });
}

std::string PairingManager::InviterKey() const
{
    return m_pending.side == Side::Inviter ? m_identity.PublicKey() : m_pending.peer->publicKey;
}

std::string PairingManager::JoinerKey() const
{
    return m_pending.side == Side::Joiner ? m_identity.PublicKey() : m_pending.peer->publicKey;
}

}  // namespace mango::remote
