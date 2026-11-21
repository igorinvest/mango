#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../session/SessionThread.hpp"
#include "RemoteModule.hpp"
#include "forwarding/ForwardingAdapter.hpp"
#include "link/PeerConnections.hpp"
#include "pairing/PairingManager.hpp"
#include "pairing/PeerStore.hpp"
#include "streaming/RemoteHost.hpp"
#include "streaming/RemoteSource.hpp"

namespace mango::remote
{

// The host side of remote streaming: the devices that play this device's
// library on their TVs ("my clients", remote/clients.json). Opens invites,
// answers the clients' offers, and serves their requests through
// RemoteSource and one ForwardingAdapter per link. Shares nothing with
// RemoteClientModule but the identity and the signaling socket (see
// docs/scope/two-way-remote-scope.md).
//
// Lives on the remote thread, except SetHost. RemoteSource lives as long
// as this, so the engine can set the host before Start.
class RemoteHostModule final : public SignalingModule
{
   public:
    RemoteHostModule(SessionThread& thread, ModuleCallbacks callbacks);
    ~RemoteHostModule() override;

    RemoteHostModule(const RemoteHostModule&) = delete;
    RemoteHostModule& operator=(const RemoteHostModule&) = delete;

    // The engine, which serves this device's library. Any thread, before
    // Start.
    void SetHost(std::shared_ptr<RemoteHost> host);

    // Makes the store, the pairing and the connections.
    void Load(const ModuleSetup& setup, bool streamingAllowed);

    // Closes every link. Nothing works afterwards.
    void Shutdown();

    void SetAppVersion(const std::string& version);
    void SetDeviceName(const std::string& name);

    // Off: clients' offers are ignored and their links close.
    void SetStreamingAllowed(bool allowed);

    void OpenInvite();
    void CancelPairing();

    // Remote streaming went off: the pairing stops and every link closes.
    void CloseAll();

    // Forgets the client: its link closes and its signals are ignored.
    void Remove(const std::string& deviceId);
    void Ping(const std::string& deviceId);

    [[nodiscard]] std::vector<RemotePeerState> Peers() const;
    [[nodiscard]] RemotePairingState Pairing() const;
    [[nodiscard]] std::size_t Count() const;
    [[nodiscard]] std::string NameOf(const std::string& deviceId) const;

    void OnSignalingReady() override;
    void OnSignalingLost() override;
    void HandleServiceMessage(const std::string& type, const std::string& text) override;
    void HandleSdpSignal(const std::string& from, const std::string& payloadText) override;
    void HandlePairSignal(const std::string& from, const std::string& payloadText) override;

   private:
    void CreatePairing(const ModuleSetup& setup);
    void CreateConnections(const ModuleSetup& setup);

    void OnMessage(const std::string& peerId, const std::string& text);
    void OnFrame(const std::string& peerId, const std::string& bytes);
    void OnLinkUp(const std::string& peerId, std::shared_ptr<LinkTransport> transport);
    void OnLinkDown(const std::string& peerId);

    void Notice(const std::string& code, const std::string& peerId, const std::string& reason = {},
        const std::string& file = {});
    [[nodiscard]] RemoteSource::Callbacks SourceCallbacks();

    SessionThread& m_thread;
    ModuleCallbacks m_callbacks;
    RemoteSource m_source;

    // Made by Load.
    std::unique_ptr<PeerStore> m_clients;
    std::unique_ptr<PairingManager> m_pairing;
    std::unique_ptr<PeerConnections> m_connections;

    // One per Ready link.
    std::map<std::string, std::unique_ptr<ForwardingAdapter>> m_adapters;
};

}  // namespace mango::remote
