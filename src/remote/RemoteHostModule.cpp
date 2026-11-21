#include "RemoteHostModule.hpp"

#include "../log/Log.hpp"
#include "forwarding/ForwardMessages.hpp"
#include "signaling/SignalingMessages.hpp"
#include "signaling/SignalingRouter.hpp"

namespace mango::remote
{

RemoteHostModule::RemoteHostModule(SessionThread& thread, ModuleCallbacks callbacks)
    : m_thread(thread), m_callbacks(std::move(callbacks)), m_source(thread, SourceCallbacks())
{
}

RemoteHostModule::~RemoteHostModule() = default;

void RemoteHostModule::SetHost(std::shared_ptr<RemoteHost> host) { m_source.SetHost(std::move(host)); }

void RemoteHostModule::Load(const ModuleSetup& setup, bool streamingAllowed)
{
    m_clients = std::make_unique<PeerStore>(setup.storePath);
    m_clients->Load();
    CreatePairing(setup);
    CreateConnections(setup);
    m_connections->SetInboundAllowed(streamingAllowed);
}

void RemoteHostModule::CreatePairing(const ModuleSetup& setup)
{
    m_pairing = std::make_unique<PairingManager>(PairingManager::Role::Inviter, m_thread, setup.identity,
        *m_clients, setup.signaling,
        PairingManager::Callbacks{
            .onChanged = [this] { m_callbacks.onChanged(); },
            .onPaired = [this](const PairedPeer& peer) { m_callbacks.onPaired(peer.deviceId); },
            .onFailed = [this](const std::string& reason) { Notice("pairingFailed", "", reason); },
        });
    m_pairing->SetDeviceName(setup.deviceName);
}

void RemoteHostModule::CreateConnections(const ModuleSetup& setup)
{
    m_connections = std::make_unique<PeerConnections>(PeerConnections::Mode::Answer, m_thread, setup.identity,
        *m_clients, setup.signaling,
        PeerConnections::Callbacks{
            .onChanged = [this] { m_callbacks.onChanged(); },
            .onNotice = [this](const std::string& code, const std::string& peerId) { Notice(code, peerId); },
            .onMessage = [this](const std::string& peerId, const std::string& text) { OnMessage(peerId, text); },
            .onBinary = [this](const std::string& peerId, std::string bytes) { OnFrame(peerId, bytes); },
            .onLinkUp = [this](const std::string& peerId, std::shared_ptr<LinkTransport> transport) {
                OnLinkUp(peerId, std::move(transport));
            },
            .onLinkDown = [this](const std::string& peerId) { OnLinkDown(peerId); },
        });
    m_connections->SetAppVersion(setup.appVersion);
}

// Everything libdatachannel holds is closed here, on the thread.
void RemoteHostModule::Shutdown()
{
    m_pairing.reset();
    m_adapters.clear();
    m_connections.reset();
}

void RemoteHostModule::SetAppVersion(const std::string& version)
{
    if (m_connections)
    {
        m_connections->SetAppVersion(version);
    }
}

void RemoteHostModule::SetDeviceName(const std::string& name)
{
    if (m_pairing)
    {
        m_pairing->SetDeviceName(name);
    }
}

void RemoteHostModule::SetStreamingAllowed(bool allowed)
{
    if (m_connections)
    {
        m_connections->SetInboundAllowed(allowed);
    }
}

void RemoteHostModule::OpenInvite()
{
    if (m_pairing)
    {
        m_pairing->OpenInvite();
    }
}

void RemoteHostModule::CancelPairing()
{
    if (m_pairing)
    {
        m_pairing->Cancel();
    }
}

void RemoteHostModule::CloseAll()
{
    CancelPairing();
    if (m_connections)
    {
        m_connections->CloseAll();
    }
}

void RemoteHostModule::Remove(const std::string& deviceId)
{
    if (!m_clients || !m_clients->Find(deviceId))
    {
        return;
    }
    Log::Info(LogCategory::Remote, "removed client {} ({})", m_clients->NameOf(deviceId), deviceId);
    m_connections->Forget(deviceId);
    m_clients->Remove(deviceId);
    m_callbacks.onChanged();
}

void RemoteHostModule::Ping(const std::string& deviceId)
{
    if (m_connections)
    {
        m_connections->Ping(deviceId);
    }
}

std::vector<RemotePeerState> RemoteHostModule::Peers() const
{
    return m_connections ? m_connections->PeerStates() : std::vector<RemotePeerState>{};
}

RemotePairingState RemoteHostModule::Pairing() const
{
    if (!m_pairing)
    {
        return {};
    }
    const auto& status = m_pairing->GetStatus();
    return {status.stage, status.link, status.expiresAtUnixSeconds};
}

std::size_t RemoteHostModule::Count() const { return m_clients ? m_clients->All().size() : 0; }

std::string RemoteHostModule::NameOf(const std::string& deviceId) const
{
    return m_clients ? m_clients->NameOf(deviceId) : deviceId.substr(0, 8);
}

// ---- From the router --------------------------------------------------------

void RemoteHostModule::OnSignalingReady()
{
    if (m_pairing)
    {
        m_pairing->OnSignalingReady();
    }
}

// Open links are direct and stay up without the service.
void RemoteHostModule::OnSignalingLost()
{
    if (m_pairing)
    {
        m_pairing->OnSignalingLost();
    }
}

void RemoteHostModule::HandleServiceMessage(const std::string& type, const std::string& text)
{
    if (!m_pairing || m_pairing->HandleMessage(type, text) || type != "error")
    {
        return;
    }
    if (const auto error = ReadSignalError(text))
    {
        m_connections->HandleSignalError(error->requestId, error->code);
    }
}

void RemoteHostModule::HandleSdpSignal(const std::string& from, const std::string& payloadText)
{
    if (m_connections)
    {
        m_connections->HandleSdpSignal(from, payloadText);
    }
}

void RemoteHostModule::HandlePairSignal(const std::string& from, const std::string& payloadText)
{
    if (m_pairing)
    {
        m_pairing->HandlePairSignal(from, payloadText);
    }
}

// ---- Links ------------------------------------------------------------------

// The link's pairing is the only authorization: every peer here is a
// client.
void RemoteHostModule::OnMessage(const std::string& peerId, const std::string& text)
{
    const auto message = signaling::Read<signaling::Envelope>(text);
    if (message && !m_source.HandleMessage(peerId, message->type, text))
    {
        Log::Debug(LogCategory::Remote, "unhandled {} from client {}", message->type, peerId.substr(0, 8));
    }
}

void RemoteHostModule::OnFrame(const std::string& peerId, const std::string& bytes)
{
    const auto frame = forward::Decode(bytes);
    const auto adapter = m_adapters.find(peerId);
    if (frame && adapter != m_adapters.end())
    {
        adapter->second->OnFrame(*frame);
    }
}

void RemoteHostModule::OnLinkUp(const std::string& peerId, std::shared_ptr<LinkTransport> transport)
{
    if (transport)
    {
        m_adapters[peerId] = std::make_unique<ForwardingAdapter>(std::move(transport), m_source.AdapterHost());
    }
}

// Cancels the client's requests: they end now rather than at their
// timeouts.
void RemoteHostModule::OnLinkDown(const std::string& peerId)
{
    Log::Info(LogCategory::Remote, "forwarding for client {} closed", peerId.substr(0, 8));
    m_adapters.erase(peerId);
}

void RemoteHostModule::Notice(
    const std::string& code, const std::string& peerId, const std::string& reason, const std::string& file)
{
    m_callbacks.onNotice(
        {.code = code, .peer = peerId.empty() ? "" : NameOf(peerId), .reason = reason, .file = file, .items = {}});
}

RemoteSource::Callbacks RemoteHostModule::SourceCallbacks()
{
    return {
        .send = [this](const std::string& peerId,
                    const std::string& text) {
            if (m_connections)
            {
                m_connections->Send(peerId, text);
            }
        },
        .onNotice = [this](const std::string& code, const std::string& peerId,
                        const std::string& file) { Notice(code, peerId, "", file); },
    };
}

}  // namespace mango::remote
