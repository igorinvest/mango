#include "RemoteClientModule.hpp"

#include "../log/Log.hpp"
#include "forwarding/ForwardMessages.hpp"
#include "signaling/SignalingMessages.hpp"
#include "signaling/SignalingRouter.hpp"

namespace mango::remote
{

RemoteClientModule::RemoteClientModule(
    SessionThread& thread, std::filesystem::path coverFolder, ModuleCallbacks callbacks)
    : m_thread(thread),
      m_callbacks(std::move(callbacks)),
      m_libraries(std::move(coverFolder), [this] { OnLibraryChanged(); }),
      m_playback(thread, PlaybackCallbacks())
{
}

RemoteClientModule::~RemoteClientModule() = default;

// ---- Any thread -------------------------------------------------------------

void RemoteClientModule::SetLibraryChangedHandler(std::function<void()> handler)
{
    std::lock_guard lock(m_handlerMutex);
    m_libraryHandler = std::move(handler);
}

std::vector<LibraryEntry> RemoteClientModule::GetLibraryEntries() const { return m_libraries.Entries(); }

std::optional<LibraryEntry> RemoteClientModule::FindLibraryEntry(const std::string& entryId) const
{
    return m_libraries.Find(entryId);
}

std::shared_ptr<ForwardingClient> RemoteClientModule::FindClient(const std::string& deviceId) const
{
    std::lock_guard lock(m_forwardingMutex);
    const auto found = m_forwarding.find(deviceId);
    return found != m_forwarding.end() ? found->second : nullptr;
}

// On the library worker: the session re-reads the entries, the state shows
// each source's library status.
void RemoteClientModule::OnLibraryChanged()
{
    std::function<void()> handler;
    {
        std::lock_guard lock(m_handlerMutex);
        handler = m_libraryHandler;
    }
    if (handler)
    {
        handler();
    }
    m_thread.Post([this] { m_callbacks.onChanged(); });
}

RemotePlayback::Callbacks RemoteClientModule::PlaybackCallbacks()
{
    return {
        .example = [this](const std::string& peerId) -> std::optional<std::string> {
            return m_connections ? m_connections->HelloExample(peerId) : std::nullopt;
        },
        .send = [this](const std::string& peerId,
                    const std::string& text) { return m_connections && m_connections->Send(peerId, text); },
        .onNotApplied = [this](const std::string& peerId,
                            const std::vector<std::string>& items) {
            Notice("settingsNotApplied", peerId, "", items);
        },
        // So the next card shows the tracks and resume point the play left
        // on the peer (see docs/scope/track-selection-scope.md, section 5).
        .onEnded = [this](const std::string& peerId) {
            Log::Info(LogCategory::Remote, "refetching {}'s library after the stream", peerId.substr(0, 8));
            RefreshLibrary(peerId);
        },
    };
}

void RemoteClientModule::Notice(const std::string& code, const std::string& peerId, const std::string& reason,
    const std::vector<std::string>& items)
{
    m_callbacks.onNotice(
        {.code = code, .peer = peerId.empty() ? "" : NameOf(peerId), .reason = reason, .file = {}, .items = items});
}

// ---- Setup ------------------------------------------------------------------

void RemoteClientModule::Load(const ModuleSetup& setup)
{
    m_sources = std::make_unique<PeerStore>(setup.storePath);
    m_sources->Load();
    CreatePairing(setup);
    CreateConnections(setup);
}

void RemoteClientModule::CreatePairing(const ModuleSetup& setup)
{
    m_pairing = std::make_unique<PairingManager>(PairingManager::Role::Joiner, m_thread, setup.identity,
        *m_sources, setup.signaling,
        PairingManager::Callbacks{
            .onChanged = [this] { m_callbacks.onChanged(); },
            .onPaired = [this](const PairedPeer& peer) { OnPaired(peer); },
            .onFailed = [this](const std::string& reason) { Notice("pairingFailed", "", reason); },
        });
    m_pairing->SetDeviceName(setup.deviceName);
}

void RemoteClientModule::CreateConnections(const ModuleSetup& setup)
{
    m_connections = std::make_unique<PeerConnections>(PeerConnections::Mode::Offer, m_thread, setup.identity,
        *m_sources, setup.signaling,
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
void RemoteClientModule::Shutdown()
{
    m_pairing.reset();
    FailForwarding();
    m_connections.reset();
}

// The new source is reachable right now.
void RemoteClientModule::OnPaired(const PairedPeer& peer)
{
    m_callbacks.onPaired(peer.deviceId);
    m_connections->Connect(peer.deviceId);
}

// ---- Commands ---------------------------------------------------------------

void RemoteClientModule::SetAppVersion(const std::string& version)
{
    if (m_connections)
    {
        m_connections->SetAppVersion(version);
    }
}

void RemoteClientModule::SetDeviceName(const std::string& name)
{
    if (m_pairing)
    {
        m_pairing->SetDeviceName(name);
    }
}

bool RemoteClientModule::JoinInvite(const std::string& link) { return m_pairing && m_pairing->Join(link); }

void RemoteClientModule::CancelPairing()
{
    if (m_pairing)
    {
        m_pairing->Cancel();
    }
}

void RemoteClientModule::CloseAll()
{
    CancelPairing();
    if (m_connections)
    {
        m_connections->CloseAll();
    }
}

void RemoteClientModule::Connect(const std::string& deviceId)
{
    if (m_connections)
    {
        m_connections->Connect(deviceId);
    }
}

void RemoteClientModule::Disconnect(const std::string& deviceId)
{
    if (m_connections)
    {
        m_connections->Disconnect(deviceId);
    }
}

void RemoteClientModule::Remove(const std::string& deviceId)
{
    if (!m_sources || !m_sources->Find(deviceId))
    {
        return;
    }
    Log::Info(LogCategory::Remote, "removed source {} ({})", m_sources->NameOf(deviceId), deviceId);
    m_connections->Forget(deviceId);
    m_sources->Remove(deviceId);
    m_libraries.Forget(deviceId);
    m_callbacks.onChanged();
}

void RemoteClientModule::Ping(const std::string& deviceId)
{
    if (m_connections)
    {
        m_connections->Ping(deviceId);
    }
}

void RemoteClientModule::RefreshLibrary(const std::string& deviceId)
{
    if (const auto client = FindClient(deviceId))
    {
        m_libraries.Fetch(deviceId, NameOf(deviceId), client);
    }
}

// ---- State ------------------------------------------------------------------

std::vector<RemotePeerState> RemoteClientModule::Peers() const
{
    auto peers = m_connections ? m_connections->PeerStates() : std::vector<RemotePeerState>{};
    for (auto& peer : peers)
    {
        const auto library = m_libraries.StatusOf(peer.deviceId);
        peer.library = library.state;
        peer.libraryCount = library.count;
    }
    return peers;
}

RemotePairingState RemoteClientModule::Pairing() const
{
    if (!m_pairing)
    {
        return {};
    }
    const auto& status = m_pairing->GetStatus();
    return {status.stage, status.link, status.expiresAtUnixSeconds};
}

std::size_t RemoteClientModule::Count() const { return m_sources ? m_sources->All().size() : 0; }

std::string RemoteClientModule::NameOf(const std::string& deviceId) const
{
    return m_sources ? m_sources->NameOf(deviceId) : deviceId.substr(0, 8);
}

// ---- From the router --------------------------------------------------------

void RemoteClientModule::OnSignalingReady()
{
    if (m_pairing)
    {
        m_pairing->OnSignalingReady();
        m_connections->OnSignalingReady();
    }
}

// Open links are direct and stay up without the service.
void RemoteClientModule::OnSignalingLost()
{
    if (m_pairing)
    {
        m_pairing->OnSignalingLost();
    }
}

void RemoteClientModule::HandleServiceMessage(const std::string& type, const std::string& text)
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

void RemoteClientModule::HandleSdpSignal(const std::string& from, const std::string& payloadText)
{
    if (m_connections)
    {
        m_connections->HandleSdpSignal(from, payloadText);
    }
}

void RemoteClientModule::HandlePairSignal(const std::string& from, const std::string& payloadText)
{
    if (m_pairing)
    {
        m_pairing->HandlePairSignal(from, payloadText);
    }
}

// ---- Links ------------------------------------------------------------------

// A source only sends answers to this device's requests.
void RemoteClientModule::OnMessage(const std::string& peerId, const std::string& text)
{
    const auto message = signaling::Read<signaling::Envelope>(text);
    if (!message)
    {
        return;
    }
    if (message->type == "initiateResult")
    {
        m_playback.HandleResult(peerId, text);
        return;
    }
    if (message->type == "transcodeStatus")
    {
        m_playback.HandleTranscodeStatus(peerId, text);
        return;
    }
    Log::Debug(LogCategory::Remote, "unhandled {} from source {}", message->type, peerId.substr(0, 8));
}

void RemoteClientModule::OnFrame(const std::string& peerId, const std::string& bytes)
{
    const auto frame = forward::Decode(bytes);
    const auto client = frame ? FindClient(peerId) : nullptr;
    if (client)
    {
        client->OnFrame(*frame);
    }
}

void RemoteClientModule::OnLinkUp(const std::string& peerId, std::shared_ptr<LinkTransport> transport)
{
    if (!transport)
    {
        return;
    }
    auto client = std::make_shared<ForwardingClient>(std::move(transport));
    {
        std::lock_guard lock(m_forwardingMutex);
        m_forwarding[peerId] = client;
    }
    m_playback.OnLinkUp(peerId);
    m_libraries.Fetch(peerId, NameOf(peerId), client);
}

// Fails the TV's requests through this link: they end now rather than at
// their timeouts.
void RemoteClientModule::OnLinkDown(const std::string& peerId)
{
    Log::Info(LogCategory::Remote, "forwarding to source {} closed", peerId.substr(0, 8));
    std::shared_ptr<ForwardingClient> client;
    {
        std::lock_guard lock(m_forwardingMutex);
        if (const auto found = m_forwarding.find(peerId); found != m_forwarding.end())
        {
            client = found->second;
            m_forwarding.erase(found);
        }
    }
    if (client)
    {
        client->FailAll();
    }
    m_playback.OnLinkDown(peerId);
}

void RemoteClientModule::FailForwarding()
{
    std::map<std::string, std::shared_ptr<ForwardingClient>> clients;
    {
        std::lock_guard lock(m_forwardingMutex);
        clients = std::exchange(m_forwarding, {});
    }
    for (auto& [peerId, client] : clients)
    {
        client->FailAll();
    }
    m_playback.FailAll();
}

}  // namespace mango::remote
