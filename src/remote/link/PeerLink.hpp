#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../../session/SessionThread.hpp"
#include "../identity/DeviceIdentity.hpp"
#include "../pairing/PeerStore.hpp"
#include "../signaling/SdpEnvelope.hpp"
#include "ChannelTransport.hpp"
#include "HelloMessage.hpp"

namespace rtc
{
class PeerConnection;
class DataChannel;
}  // namespace rtc

namespace mango::remote
{

// What every PeerLink shares: owned by PeerConnections, outlives its links.
struct PeerLinkContext
{
    SessionThread& thread;
    const DeviceIdentity& identity;
    std::string appVersion;
    std::vector<std::string> iceServers;

    // Sends an offer or answer through the signaling service.
    std::function<void(const SdpPayload& payload)> sendSdp;

    // The link's state (or its ping time) changed.
    std::function<void(const std::string& peerId)> onChanged;

    // A message after the handshake that the link doesn't handle itself.
    std::function<void(const std::string& peerId, const std::string& text)> onMessage;

    // A binary message (a forwarding frame) after the handshake.
    std::function<void(const std::string& peerId, std::string bytes)> onBinary;
};

// One WebRTC connection to a paired peer: one reliable, ordered data
// channel, opened with a single signed offer and answer (no trickle ICE),
// then the hello handshake. B offers, A answers.
//
// Lives on the context's thread: every method is called there, and
// libdatachannel's callbacks are posted there.
class PeerLink : public std::enable_shared_from_this<PeerLink>
{
   public:
    enum class State
    {
        Connecting,
        Handshaking,
        Ready,
        Closed,
        Failed,
        PeerNeedsUpdate,
        UpdateRequired,
    };

    // sessionId: the offerer's fresh one, or the offer's for an answerer.
    static std::shared_ptr<PeerLink> Create(
        PeerLinkContext& context, PairedPeer peer, bool offerer, std::string sessionId);
    ~PeerLink();

    PeerLink(const PeerLink&) = delete;
    PeerLink& operator=(const PeerLink&) = delete;

    // The offerer makes its offer; the answerer waits for HandleSdp.
    void Start();

    // An offer (answerer) or answer (offerer) whose signature was checked.
    void HandleSdp(const SdpPayload& payload);

    // Text message to the peer; false unless Ready.
    bool Send(const std::string& text);

    // Measures the round trip; see LastPing.
    void Ping();

    void Close();

    [[nodiscard]] State GetState() const { return m_state; }
    [[nodiscard]] bool IsOfferer() const { return m_offerer; }
    [[nodiscard]] const std::string& SessionId() const { return m_sessionId; }
    [[nodiscard]] const PairedPeer& Peer() const { return m_peer; }
    [[nodiscard]] const std::optional<PeerHello>& PeerHelloMessage() const { return m_peerHello; }
    [[nodiscard]] std::optional<std::chrono::milliseconds> LastPing() const { return m_lastPing; }

    // Binary frames to the peer from any thread, for HTTP forwarding. Set
    // once Ready; its sends fail once the link is finished.
    [[nodiscard]] std::shared_ptr<LinkTransport> Transport() const { return m_transport; }

    [[nodiscard]] static std::string ToString(State state);
    [[nodiscard]] static bool IsFinished(State state);

   private:
    PeerLink(PeerLinkContext& context, PairedPeer peer, bool offerer, std::string sessionId);

    void CreateConnection();
    void WatchConnection();

    // What libdatachannel's callbacks hold instead of `this`: posts a task
    // to the thread, dropped once the link is gone or finished. Safe from
    // any thread, even after the link was destroyed.
    struct Poster
    {
        std::weak_ptr<PeerLink> link;
        SessionThread* thread;

        void operator()(std::function<void(PeerLink&)> task) const;
    };

    [[nodiscard]] Poster MakePoster() { return {weak_from_this(), &m_context.thread}; }

    static void WatchChannel(const std::shared_ptr<rtc::DataChannel>& channel, const Poster& post);

    void OnGatheringComplete();
    void OnConnectionFailed();
    void OnChannelOpen();
    void OnChannelClosed();
    void OnText(const std::string& text);
    void OnBinary(std::string bytes);
    void HandleHello(const std::string& text);
    void HandlePing(const std::string& text);
    void HandlePong(const std::string& text);

    void SendRaw(const std::string& text);
    void SetState(State state);
    void ScheduleTimeout();
    void Teardown();

    PeerLinkContext& m_context;
    const PairedPeer m_peer;
    const bool m_offerer;
    const std::string m_sessionId;

    std::shared_ptr<rtc::PeerConnection> m_connection;
    std::shared_ptr<rtc::DataChannel> m_channel;
    std::shared_ptr<ChannelTransport> m_transport;
    State m_state = State::Connecting;
    bool m_sentSdp = false;

    std::optional<PeerHello> m_peerHello;
    std::optional<std::chrono::milliseconds> m_lastPing;
    SessionThread::TimerId m_timeout = 0;
};

}  // namespace mango::remote
