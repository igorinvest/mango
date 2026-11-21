#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "../../session/SessionThread.hpp"
#include "../identity/DeviceIdentity.hpp"
#include "../signaling/SdpEnvelope.hpp"
#include "../signaling/SignalingClient.hpp"
#include "../signaling/SignalingMessages.hpp"
#include "PairingProof.hpp"
#include "PeerStore.hpp"

namespace mango::remote
{

// Pairing through the signaling service (signal/README.md):
// A opens an invite and shows its link; B joins it. The service introduces
// the two, and each then proves to the other, in one "signal" round trip,
// that it holds the invite's secret. The peer is saved only once the other
// side's proof checks out.
//
// Each manager has one role for its life: the host module's invites, the
// client module's joins. It saves the peer to its module's store.
//
// Lives on the remote thread.
class PairingManager
{
   public:
    enum class Role
    {
        Inviter,
        Joiner,
    };

    // "idle", "opening" (A, waiting for the service), "waitingForPeer" (A,
    // the link is shown), "joining" (B, waiting for the service),
    // "confirming" (both, exchanging proofs).
    struct Status
    {
        std::string stage = "idle";
        std::string link;
        std::int64_t expiresAtUnixSeconds = 0;
    };

    struct Callbacks
    {
        std::function<void()> onChanged;
        std::function<void(const PairedPeer& peer)> onPaired;
        // reason: "timeout", "expired", "wrongPeer", "badProof", "offline",
        // "notFound", "limit", "signaling" or the service's error code.
        std::function<void(const std::string& reason)> onFailed;
    };

    PairingManager(Role role, SessionThread& thread, const DeviceIdentity& identity, PeerStore& peers,
        SignalingClient& signaling, Callbacks callbacks);
    ~PairingManager();

    PairingManager(const PairingManager&) = delete;
    PairingManager& operator=(const PairingManager&) = delete;

    // The name the peer will show this device under.
    void SetDeviceName(std::string name) { m_deviceName = std::move(name); }

    // Inviter: opens an invite once the service is ready. Replaces an open one.
    void OpenInvite();

    // Joiner: joins A's invite. false when link isn't an invite link, or
    // this is the inviter.
    bool Join(const std::string& link);

    // Either side: gives up the invite or join in progress.
    void Cancel();

    // The service is ready: sends what was waiting for it.
    void OnSignalingReady();

    // The socket closed: an invite or join in progress fails.
    void OnSignalingLost();

    // Messages from the service: true when this handled it.
    bool HandleMessage(const std::string& type, const std::string& text);

    // A "signal" whose payload is a pairing proof.
    void HandlePairSignal(const std::string& from, const std::string& payloadText);

    [[nodiscard]] const Status& GetStatus() const { return m_status; }

   private:
    enum class Side
    {
        None,
        Inviter,
        Joiner,
    };

    // The pairing in progress, from either side.
    struct Pending
    {
        Side side = Side::None;
        std::string secret;
        std::string inviteHash;
        // Joiner: the deviceId the link names.
        std::string expectedDeviceId;
        // Set once the service introduced the peer.
        std::optional<signaling::PeerKey> peer;
        // Joiner: A's proof, when it came before invite.accepted.
        std::optional<std::string> earlyProofPayload;
        std::string requestId;
        bool sentProof = false;
    };

    void SendInviteOpen();
    void SendInviteJoin();
    void HandleInviteOpened(const std::string& text);
    void HandleInviteJoined(const std::string& text);
    void HandleInviteAccepted(const std::string& text);
    void HandleError(const std::string& text);

    // Both sides, once the service introduced the peer.
    void Introduce(const signaling::PeerKey& peer);
    void SendProof();
    void CheckProof(const PairPayload& payload);
    void Finish(const std::string& peerName);
    void Fail(const std::string& reason);
    void Reset();

    void SetStage(std::string stage);
    void StartTimer(std::chrono::seconds timeout, std::string reason);

    [[nodiscard]] std::string InviterKey() const;
    [[nodiscard]] std::string JoinerKey() const;

    const Role m_role;
    SessionThread& m_thread;
    const DeviceIdentity& m_identity;
    PeerStore& m_peers;
    SignalingClient& m_signaling;
    Callbacks m_callbacks;
    std::string m_deviceName;

    Pending m_pending;
    Status m_status;
    SessionThread::TimerId m_timer = 0;
};

}  // namespace mango::remote
