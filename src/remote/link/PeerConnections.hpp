#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

#include "../RemoteState.hpp"
#include "../pairing/PeerStore.hpp"
#include "../signaling/SignalingClient.hpp"
#include "PeerLink.hpp"

namespace mango::remote
{

// How one paired peer's connection is doing, for the state snapshot.
struct PeerLinkStatus
{
    // "disconnected", "connecting", "handshaking", "connected", "failed",
    // "offline" (the service says the peer isn't connected),
    // "peerNeedsUpdate" or "updateRequired" (a protocol mismatch).
    std::string link = "disconnected";
    std::string peerAppVersion;
    std::optional<std::int64_t> pingMs;
};

// One module's PeerLinks, one per peer in its store. The client module's
// (Mode::Offer) offers to its sources as soon as the signaling service is
// ready, and retries with backoff while they're offline or the link drops.
// The host module's (Mode::Answer) answers offers from its clients and
// never offers. Signals from devices that aren't in the store, SDPs of the
// wrong kind for the mode, and SDPs whose signature doesn't match the key
// saved at pairing are ignored.
//
// Lives on the remote thread.
class PeerConnections
{
   public:
    enum class Mode
    {
        Offer,
        Answer,
    };

    struct Callbacks
    {
        // Something in Status changed.
        std::function<void()> onChanged;
        // A notice for the user: "peerConnectionFailed" or "peerLinkLost".
        std::function<void(const std::string& code, const std::string& peerId)> onNotice;
        // A message from a Ready link that the link doesn't handle itself.
        std::function<void(const std::string& peerId, const std::string& text)> onMessage;

        // A binary frame from a Ready link.
        std::function<void(const std::string& peerId, std::string bytes)> onBinary;

        // The link finished its handshake: frames go through transport.
        std::function<void(const std::string& peerId, std::shared_ptr<LinkTransport> transport)> onLinkUp;

        // The link that was up is gone (closed, lost or replaced).
        std::function<void(const std::string& peerId)> onLinkDown;
    };

    PeerConnections(Mode mode, SessionThread& thread, const DeviceIdentity& identity, const PeerStore& peers,
        SignalingClient& signaling, Callbacks callbacks);
    ~PeerConnections();

    PeerConnections(const PeerConnections&) = delete;
    PeerConnections& operator=(const PeerConnections&) = delete;

    void SetAppVersion(std::string version) { m_context.appVersion = std::move(version); }

    // Offer mode: offers to every peer that isn't connected.
    void OnSignalingReady();

    // Offer mode: offers to peerId now (and keeps retrying); false if it
    // isn't in the store, or this is Answer mode.
    bool Connect(const std::string& peerId);

    // Closes the link and stops retrying until Connect or the next start.
    void Disconnect(const std::string& peerId);

    // The peer was revoked: closes its link and forgets it.
    void Forget(const std::string& peerId);

    void CloseAll();

    // Answer mode. Off: offers are ignored and the links they opened are
    // closed.
    void SetInboundAllowed(bool allowed);

    // A "signal" whose payload is an offer or answer.
    void HandleSdpSignal(const std::string& from, const std::string& payloadText);

    // The service couldn't deliver the signal sent with this request ID.
    void HandleSignalError(const std::string& requestId, const std::string& code);

    bool Ping(const std::string& peerId);
    bool Send(const std::string& peerId, const std::string& text);

    // The initiateExample of the peer's hello while its link is Ready.
    [[nodiscard]] std::optional<std::string> HelloExample(const std::string& peerId) const;

    [[nodiscard]] PeerLinkStatus Status(const std::string& peerId) const;

    // Every peer in the store with its link's status, for the state (no
    // library fields).
    [[nodiscard]] std::vector<RemotePeerState> PeerStates() const;

   private:
    // Closes the links paired peers opened to this device.
    void CloseInbound();

    struct Retry
    {
        bool wanted = false;
        // How the last link ended, shown while there's none.
        std::string lastLink = "disconnected";
        bool failureNoticed = false;
        std::chrono::seconds backoff{5};
        SessionThread::TimerId timer = 0;
    };

    void Offer(const PairedPeer& peer);
    void Answer(const PairedPeer& peer, const SdpPayload& offer);
    void SendSdp(const SdpPayload& payload);
    void OnLinkChanged(const std::string& peerId);
    void OnLinkFinished(const std::string& peerId, PeerLink::State state);
    void NoteFailure(const std::string& peerId, Retry& retry);
    void ScheduleRetry(const std::string& peerId);
    void CloseLink(const std::string& peerId);
    void NoteLinkDown(const std::string& peerId);

    [[nodiscard]] std::shared_ptr<PeerLink> FindLink(const std::string& peerId) const;
    [[nodiscard]] bool IsNewSession(const std::string& sessionId);

    const Mode m_mode;
    SessionThread& m_thread;
    const PeerStore& m_peers;
    SignalingClient& m_signaling;
    Callbacks m_callbacks;
    PeerLinkContext m_context;

    std::map<std::string, std::shared_ptr<PeerLink>> m_links;
    std::map<std::string, Retry> m_retries;
    bool m_inboundAllowed = true;

    // A signal's request ID → the peer it went to, to match an "offline" error.
    std::map<std::string, std::string> m_signalTargets;

    // Session IDs already seen, so a replayed SDP is ignored.
    std::set<std::string> m_seenSessions;

    // Peers whose link is Ready, so onLinkDown is called once per onLinkUp.
    std::set<std::string> m_up;
};

}  // namespace mango::remote
