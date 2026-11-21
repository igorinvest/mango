#include "PeerLink.hpp"

#include <variant>

#include <rtc/rtc.hpp>

#include "../../log/Log.hpp"
#include "../signaling/SignalingMessages.hpp"

namespace mango::remote
{

// "ping" and "pong" on the data channel. sentAtMs is the sender's own
// steady clock, echoed back in the pong. Not in an anonymous namespace:
// glaze needs a type with linkage.
struct PingMessage
{
    std::string type = "ping";
    std::int64_t sentAtMs = 0;
};

}  // namespace mango::remote

template <>
struct glz::meta<mango::remote::PingMessage>
{
    using T = mango::remote::PingMessage;
    static constexpr auto value = object("type", &T::type, "sentAtMs", &T::sentAtMs);
};

namespace mango::remote
{

namespace
{

// From Start to a finished handshake. Gathering alone can take a few
// seconds per STUN server that doesn't answer.
constexpr std::chrono::seconds kConnectTimeout{30};

constexpr std::string_view kChannelLabel = "mango";

std::int64_t SteadyNowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string ShortId(const std::string& deviceId) { return deviceId.substr(0, 8); }

}  // namespace

std::shared_ptr<PeerLink> PeerLink::Create(
    PeerLinkContext& context, PairedPeer peer, bool offerer, std::string sessionId)
{
    return std::shared_ptr<PeerLink>(new PeerLink(context, std::move(peer), offerer, std::move(sessionId)));
}

PeerLink::PeerLink(PeerLinkContext& context, PairedPeer peer, bool offerer, std::string sessionId)
    : m_context(context), m_peer(std::move(peer)), m_offerer(offerer), m_sessionId(std::move(sessionId))
{
}

PeerLink::~PeerLink() { Teardown(); }

void PeerLink::Start()
{
    Log::Info(LogCategory::Remote, "link to {} ({}): {}", m_peer.name, ShortId(m_peer.deviceId),
        m_offerer ? "offering" : "waiting for the offer");
    CreateConnection();
    ScheduleTimeout();

    if (m_offerer)
    {
        // Creating the channel starts negotiation: the offer is sent once
        // its candidates are gathered.
        auto channel = m_connection->createDataChannel(std::string(kChannelLabel));
        m_channel = channel;
        WatchChannel(channel, MakePoster());
    }
}

void PeerLink::HandleSdp(const SdpPayload& payload)
{
    if (!m_connection || IsFinished(m_state))
    {
        return;
    }

    const bool expected = m_offerer ? payload.kind == "answer" : payload.kind == "offer";
    if (!expected || payload.sessionId != m_sessionId)
    {
        Log::Warning(LogCategory::Remote, "link to {}: unexpected {}", ShortId(m_peer.deviceId), payload.kind);
        return;
    }

    try
    {
        m_connection->setRemoteDescription(rtc::Description(payload.sdp, payload.kind));
    }
    catch (const std::exception& error)
    {
        Log::Warning(LogCategory::Remote, "link to {}: bad {}: {}", ShortId(m_peer.deviceId), payload.kind,
            error.what());
        OnConnectionFailed();
    }
}

bool PeerLink::Send(const std::string& text)
{
    if (m_state != State::Ready)
    {
        return false;
    }
    SendRaw(text);
    return true;
}

void PeerLink::Ping() { Send(signaling::Write(PingMessage{.sentAtMs = SteadyNowMs()})); }

void PeerLink::Close()
{
    if (!IsFinished(m_state))
    {
        SetState(State::Closed);
    }
    Teardown();
}

std::string PeerLink::ToString(State state)
{
    switch (state)
    {
        case State::Connecting: return "connecting";
        case State::Handshaking: return "handshaking";
        case State::Ready: return "connected";
        case State::Closed: return "disconnected";
        case State::Failed: return "failed";
        case State::PeerNeedsUpdate: return "peerNeedsUpdate";
        case State::UpdateRequired: return "updateRequired";
    }
    return "disconnected";
}

bool PeerLink::IsFinished(State state)
{
    return state != State::Connecting && state != State::Handshaking && state != State::Ready;
}

void PeerLink::CreateConnection()
{
    rtc::Configuration configuration;
    for (const auto& server : m_context.iceServers)
    {
        configuration.iceServers.emplace_back(server);
    }

    m_connection = std::make_shared<rtc::PeerConnection>(configuration);
    WatchConnection();
}

void PeerLink::WatchConnection()
{
    const auto post = MakePoster();

    m_connection->onGatheringStateChange([post](rtc::PeerConnection::GatheringState state) {
        if (state == rtc::PeerConnection::GatheringState::Complete)
        {
            post([](PeerLink& link) { link.OnGatheringComplete(); });
        }
    });

    m_connection->onStateChange([post](rtc::PeerConnection::State state) {
        if (state == rtc::PeerConnection::State::Failed || state == rtc::PeerConnection::State::Disconnected)
        {
            post([](PeerLink& link) { link.OnConnectionFailed(); });
        }
    });

    // Watched right here, on libdatachannel's thread: the peer's hello can
    // arrive straight after, before a posted task would have run.
    m_connection->onDataChannel([post](std::shared_ptr<rtc::DataChannel> channel) {
        WatchChannel(channel, post);
        post([channel = std::move(channel)](PeerLink& link) {
            if (!link.m_channel)
            {
                link.m_channel = channel;
                if (channel->isOpen())
                {
                    link.OnChannelOpen();
                }
            }
        });
    });
}

void PeerLink::WatchChannel(const std::shared_ptr<rtc::DataChannel>& channel, const Poster& post)
{
    channel->onOpen([post] { post([](PeerLink& link) { link.OnChannelOpen(); }); });
    channel->onClosed([post] { post([](PeerLink& link) { link.OnChannelClosed(); }); });
    channel->onMessage([post](rtc::message_variant message) {
        if (auto* text = std::get_if<std::string>(&message))
        {
            post([text = std::move(*text)](PeerLink& link) { link.OnText(text); });
        }
        else if (auto* binary = std::get_if<rtc::binary>(&message))
        {
            std::string bytes(reinterpret_cast<const char*>(binary->data()), binary->size());
            post([bytes = std::move(bytes)](PeerLink& link) mutable { link.OnBinary(std::move(bytes)); });
        }
    });
}

void PeerLink::Poster::operator()(std::function<void(PeerLink&)> task) const
{
    thread->Post([weak = link, task = std::move(task)] {
        if (const auto link = weak.lock(); link && !IsFinished(link->m_state))
        {
            task(*link);
        }
    });
}

void PeerLink::OnGatheringComplete()
{
    const auto description = m_connection->localDescription();
    if (m_sentSdp || !description)
    {
        return;
    }
    m_sentSdp = true;

    const auto kind = m_offerer ? "offer" : "answer";
    Log::Info(LogCategory::Remote, "link to {}: sending the {}", ShortId(m_peer.deviceId), kind);
    m_context.sendSdp(SdpEnvelope::Make(m_context.identity, kind, std::string(*description), m_peer.deviceId,
        m_sessionId, SdpEnvelope::NowUnixSeconds()));
}

void PeerLink::OnConnectionFailed()
{
    Log::Warning(LogCategory::Remote, "link to {} {}", ShortId(m_peer.deviceId),
        m_state == State::Ready ? "lost" : "failed");
    SetState(m_state == State::Ready ? State::Closed : State::Failed);
    Teardown();
}

void PeerLink::OnChannelOpen()
{
    if (m_state != State::Connecting)
    {
        return;
    }
    SetState(State::Handshaking);
    SendRaw(Hello::Make(m_context.appVersion));
}

void PeerLink::OnChannelClosed()
{
    Log::Info(LogCategory::Remote, "link to {}: channel closed", ShortId(m_peer.deviceId));
    SetState(m_state == State::Ready ? State::Closed : State::Failed);
    Teardown();
}

void PeerLink::OnText(const std::string& text)
{
    const auto message = signaling::Read<signaling::Envelope>(text);
    if (!message)
    {
        return;
    }

    // Nothing else is read before the handshake.
    if (message->type == "hello")
    {
        HandleHello(text);
    }
    else if (m_state != State::Ready)
    {
        return;
    }
    else if (message->type == "ping")
    {
        HandlePing(text);
    }
    else if (message->type == "pong")
    {
        HandlePong(text);
    }
    else if (m_context.onMessage)
    {
        m_context.onMessage(m_peer.deviceId, text);
    }
}

void PeerLink::OnBinary(std::string bytes)
{
    if (m_state == State::Ready && m_context.onBinary)
    {
        m_context.onBinary(m_peer.deviceId, std::move(bytes));
    }
}

void PeerLink::HandleHello(const std::string& text)
{
    auto hello = Hello::Read(text);
    if (!hello || m_state != State::Handshaking)
    {
        return;
    }

    m_peerHello = std::move(hello);
    const auto compatibility = Hello::Check(m_peerHello->protocol);
    Log::Info(LogCategory::Remote, "link to {}: hello from Mango {} (protocol {})", ShortId(m_peer.deviceId),
        m_peerHello->appVersion, m_peerHello->protocol);

    if (compatibility == Compatibility::Compatible)
    {
        m_context.thread.Cancel(std::exchange(m_timeout, 0));
        m_transport = std::make_shared<ChannelTransport>(m_channel);
        SetState(State::Ready);
        return;
    }

    SetState(compatibility == Compatibility::PeerNeedsUpdate ? State::PeerNeedsUpdate : State::UpdateRequired);
    Teardown();
}

void PeerLink::HandlePing(const std::string& text)
{
    if (auto ping = signaling::Read<PingMessage>(text))
    {
        ping->type = "pong";
        SendRaw(signaling::Write(*ping));
    }
}

void PeerLink::HandlePong(const std::string& text)
{
    if (const auto pong = signaling::Read<PingMessage>(text))
    {
        m_lastPing = std::chrono::milliseconds(SteadyNowMs() - pong->sentAtMs);
        Log::Info(LogCategory::Remote, "link to {}: ping {} ms", ShortId(m_peer.deviceId), m_lastPing->count());
        m_context.onChanged(m_peer.deviceId);
    }
}

void PeerLink::SendRaw(const std::string& text)
{
    try
    {
        if (m_channel && m_channel->isOpen())
        {
            m_channel->send(text);
        }
    }
    catch (const std::exception& error)
    {
        Log::Warning(LogCategory::Remote, "link to {}: send failed: {}", ShortId(m_peer.deviceId), error.what());
    }
}

void PeerLink::SetState(State state)
{
    if (state == m_state)
    {
        return;
    }
    m_state = state;
    Log::Info(LogCategory::Remote, "link to {}: {}", ShortId(m_peer.deviceId), ToString(state));
    m_context.onChanged(m_peer.deviceId);
}

void PeerLink::ScheduleTimeout()
{
    m_timeout = m_context.thread.PostAfter(kConnectTimeout, [weak = weak_from_this()] {
        const auto link = weak.lock();
        if (!link)
        {
            return;
        }
        link->m_timeout = 0;
        if (link->m_state == State::Connecting || link->m_state == State::Handshaking)
        {
            Log::Warning(LogCategory::Remote, "link to {}: no connection after {} s",
                ShortId(link->m_peer.deviceId), kConnectTimeout.count());
            link->SetState(State::Failed);
            link->Teardown();
        }
    });
}

// Resets callbacks first: that waits for one running now, so none runs
// after this returns.
void PeerLink::Teardown()
{
    m_context.thread.Cancel(std::exchange(m_timeout, 0));
    if (m_transport)
    {
        m_transport->Detach();
    }

    if (auto channel = std::exchange(m_channel, nullptr))
    {
        channel->resetCallbacks();
        try
        {
            channel->close();
        }
        catch (const std::exception&)
        {
        }
    }

    if (auto connection = std::exchange(m_connection, nullptr))
    {
        connection->resetCallbacks();
        try
        {
            connection->close();
        }
        catch (const std::exception&)
        {
        }
    }
}

}  // namespace mango::remote
