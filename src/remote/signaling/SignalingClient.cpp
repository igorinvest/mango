#include "SignalingClient.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <variant>

#include <rtc/rtc.hpp>

#include "../../log/Log.hpp"
#include "SignalingMessages.hpp"

namespace mango::remote
{

namespace
{

// Cloudflare closes sockets idle for 100 s.
constexpr std::chrono::seconds kPingInterval{45};

// From opening the socket to "ready".
constexpr std::chrono::seconds kConnectTimeout{20};

constexpr std::chrono::seconds kMaxBackoff{60};

constexpr std::string_view kAuthPrefix = "mango-signal-v1 auth:";

// vcpkg's OpenSSL looks for root certificates in its own install folder,
// where there are none: use the system's bundle where there is one. Windows
// has no such file (libdatachannel doesn't verify there). The design
// doesn't depend on this TLS: pairing and every SDP are checked end to end.
std::optional<std::string> SystemCaBundle()
{
    static constexpr std::array<const char*, 3> kCandidates{
        "/etc/ssl/cert.pem",                   // macOS
        "/etc/ssl/certs/ca-certificates.crt",  // Debian, Ubuntu
        "/etc/pki/tls/certs/ca-bundle.crt",    // Fedora
    };

    std::error_code ignored;
    const auto found = std::ranges::find_if(
        kCandidates, [&](const char* path) { return std::filesystem::exists(path, ignored); });
    return found != kCandidates.end() ? std::optional<std::string>(*found) : std::nullopt;
}

rtc::WebSocket::Configuration SocketConfiguration()
{
    rtc::WebSocket::Configuration configuration;
    configuration.caCertificatePemFile = SystemCaBundle();
    configuration.connectionTimeout = kConnectTimeout;
    // The service pings us; ours are JSON pings (see SchedulePing).
    configuration.pingInterval = std::chrono::milliseconds(0);
    configuration.maxMessageSize = 64 * 1024;
    return configuration;
}

}  // namespace

SignalingClient::SignalingClient(
    const DeviceIdentity& identity, SessionThread& thread, std::string url, Handlers handlers)
    : m_identity(identity), m_thread(thread), m_url(std::move(url)), m_handlers(std::move(handlers))
{
}

SignalingClient::~SignalingClient() { CloseSocket(); }

void SignalingClient::Start()
{
    if (m_wanted)
    {
        return;
    }
    if (m_url.empty())
    {
        Log::Warning(LogCategory::Remote, "signaling: no service URL (\"remoteSignalingUrl\" in settings.json)");
        return;
    }
    m_wanted = true;
    m_backoff = std::chrono::seconds(1);
    Connect();
}

void SignalingClient::Stop()
{
    if (!m_wanted)
    {
        return;
    }
    m_wanted = false;
    CancelTimers();
    CloseSocket();
    const bool wasConnected = m_state != State::Stopped;
    m_state = State::Stopped;
    Log::Info(LogCategory::Remote, "signaling stopped");
    if (wasConnected && m_handlers.onDisconnected)
    {
        m_handlers.onDisconnected();
    }
}

bool SignalingClient::Send(const std::string& json)
{
    if (m_state != State::Ready || !m_socket)
    {
        return false;
    }

    try
    {
        return m_socket->send(json);
    }
    catch (const std::exception& error)
    {
        Log::Warning(LogCategory::Remote, "signaling send failed: {}", error.what());
        return false;
    }
}

std::string SignalingClient::NextRequestId() { return "m" + std::to_string(m_nextRequestId++); }

void SignalingClient::Connect()
{
    CloseSocket();
    m_state = State::Connecting;

    auto socket = std::make_shared<rtc::WebSocket>(SocketConfiguration());
    m_socket = socket;
    Watch(socket);

    Log::Info(LogCategory::Remote, "signaling: connecting to {}", m_url);
    try
    {
        socket->open(m_url);
    }
    catch (const std::exception& error)
    {
        Log::Warning(LogCategory::Remote, "signaling: can't open {}: {}", m_url, error.what());
        OnClosed();
        return;
    }

    // Covers a socket that opens but never gets as far as "ready".
    m_authTimer = m_thread.PostAfter(kConnectTimeout, [this] {
        m_authTimer = 0;
        if (m_state == State::Connecting)
        {
            Log::Warning(LogCategory::Remote, "signaling: no ready after {} s", kConnectTimeout.count());
            OnClosed();
        }
    });
}

void SignalingClient::Watch(const std::shared_ptr<rtc::WebSocket>& socket)
{
    const auto generation = m_generation;

    socket->onMessage([this, generation](rtc::message_variant message) {
        if (auto* text = std::get_if<std::string>(&message))
        {
            PostForSocket(generation, [this, text = std::move(*text)] { OnText(text); });
        }
    });
    socket->onClosed([this, generation] { PostForSocket(generation, [this] { OnClosed(); }); });
    socket->onError([this, generation](std::string error) {
        PostForSocket(generation, [error = std::move(error)] {
            Log::Warning(LogCategory::Remote, "signaling socket error: {}", error);
        });
    });
}

void SignalingClient::PostForSocket(std::uint64_t generation, std::function<void()> task)
{
    m_thread.Post([this, generation, task = std::move(task)] {
        if (generation == m_generation)
        {
            task();
        }
    });
}

// Waits for a callback running right now, so none runs after this returns.
void SignalingClient::CloseSocket()
{
    ++m_generation;
    if (!m_socket)
    {
        return;
    }

    auto socket = std::move(m_socket);
    socket->resetCallbacks();
    try
    {
        socket->close();
    }
    catch (const std::exception&)
    {
        // Closing a socket that never opened can throw; it's gone either way.
    }
}

void SignalingClient::OnText(const std::string& text)
{
    const auto envelope = signaling::Read<signaling::Envelope>(text);
    if (!envelope)
    {
        Log::Warning(LogCategory::Remote, "signaling: unreadable message");
        return;
    }

    if (envelope->type == "challenge")
    {
        HandleChallenge(text);
    }
    else if (envelope->type == "ready")
    {
        HandleReady(text);
    }
    else if (envelope->type != "pong" && m_handlers.onMessage)
    {
        m_handlers.onMessage(envelope->type, text);
    }
}

void SignalingClient::HandleChallenge(const std::string& text)
{
    const auto challenge = signaling::Read<signaling::Challenge>(text);
    if (!challenge || !m_socket)
    {
        return;
    }

    const signaling::Auth auth{.publicKey = m_identity.PublicKey(),
        .signature = m_identity.Sign(std::string(kAuthPrefix) + challenge->nonce)};
    try
    {
        m_socket->send(signaling::Write(auth));
    }
    catch (const std::exception& error)
    {
        Log::Warning(LogCategory::Remote, "signaling: can't send auth: {}", error.what());
    }
}

void SignalingClient::HandleReady(const std::string& text)
{
    const auto ready = signaling::Read<signaling::Ready>(text);
    if (ready && ready->deviceId != m_identity.DeviceId())
    {
        Log::Warning(LogCategory::Remote, "signaling: the service calls this device {}", ready->deviceId);
    }

    m_thread.Cancel(std::exchange(m_authTimer, 0));
    m_state = State::Ready;
    m_backoff = std::chrono::seconds(1);
    Log::Info(LogCategory::Remote, "signaling ready as {}", m_identity.DeviceId());
    SchedulePing();

    if (m_handlers.onReady)
    {
        m_handlers.onReady();
    }
}

void SignalingClient::OnClosed()
{
    const bool wasReady = m_state == State::Ready;
    CancelTimers();
    CloseSocket();
    m_state = m_wanted ? State::Connecting : State::Stopped;

    Log::Info(LogCategory::Remote, "signaling disconnected");
    if (wasReady && m_handlers.onDisconnected)
    {
        m_handlers.onDisconnected();
    }

    if (m_wanted)
    {
        ScheduleReconnect();
    }
}

void SignalingClient::SchedulePing()
{
    m_pingTimer = m_thread.PostAfter(kPingInterval, [this] {
        m_pingTimer = 0;
        if (m_state == State::Ready)
        {
            Send(signaling::Write(signaling::Ping{}));
            SchedulePing();
        }
    });
}

void SignalingClient::ScheduleReconnect()
{
    Log::Info(LogCategory::Remote, "signaling: reconnecting in {} s", m_backoff.count());
    m_reconnectTimer = m_thread.PostAfter(m_backoff, [this] {
        m_reconnectTimer = 0;
        if (m_wanted)
        {
            Connect();
        }
    });
    m_backoff = std::min(m_backoff * 2, kMaxBackoff);
}

void SignalingClient::CancelTimers()
{
    m_thread.Cancel(std::exchange(m_pingTimer, 0));
    m_thread.Cancel(std::exchange(m_reconnectTimer, 0));
    m_thread.Cancel(std::exchange(m_authTimer, 0));
}

}  // namespace mango::remote
