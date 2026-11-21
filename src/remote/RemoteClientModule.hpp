#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "../session/SessionThread.hpp"
#include "RemoteModule.hpp"
#include "forwarding/ForwardingClient.hpp"
#include "link/PeerConnections.hpp"
#include "pairing/PairingManager.hpp"
#include "pairing/PeerStore.hpp"
#include "streaming/RemoteLibraries.hpp"
#include "streaming/RemotePlayback.hpp"

namespace mango::remote
{

// The client side of remote streaming: the devices whose libraries this
// one plays on its TV ("my sources", remote/sources.json). Joins invites,
// offers to every source and keeps the links up, and over them loads the
// sources' libraries, forwards the TV's requests (ForwardingClient) and
// starts remote streams (RemotePlayback). Shares nothing with
// RemoteHostModule but the identity and the signaling socket (see
// docs/scope/two-way-remote-scope.md).
//
// Lives on the remote thread, except the "any thread" methods. The
// libraries and playback live as long as this, so the engine's threads can
// use them without racing Shutdown.
class RemoteClientModule final : public SignalingModule
{
   public:
    RemoteClientModule(SessionThread& thread, std::filesystem::path coverFolder, ModuleCallbacks callbacks);
    ~RemoteClientModule() override;

    RemoteClientModule(const RemoteClientModule&) = delete;
    RemoteClientModule& operator=(const RemoteClientModule&) = delete;

    // ---- Any thread

    // Called on a worker after a source's library changed.
    void SetLibraryChangedHandler(std::function<void()> handler);
    [[nodiscard]] std::vector<LibraryEntry> GetLibraryEntries() const;
    [[nodiscard]] std::optional<LibraryEntry> FindLibraryEntry(const std::string& entryId) const;

    // The connected source's forwarding client, for /remote/...
    [[nodiscard]] std::shared_ptr<ForwardingClient> FindClient(const std::string& deviceId) const;

    [[nodiscard]] RemotePlayback& Playback() { return m_playback; }

    // ---- On the thread

    // Makes the store, the pairing and the connections.
    void Load(const ModuleSetup& setup);

    // Closes every link and fails what waits on one. Nothing works
    // afterwards.
    void Shutdown();

    void SetAppVersion(const std::string& version);
    void SetDeviceName(const std::string& name);

    // false when link isn't an invite link.
    bool JoinInvite(const std::string& link);
    void CancelPairing();

    // Remote streaming went off: the pairing stops and every link closes.
    void CloseAll();

    // The links are made by themselves; these are for a UI and tests.
    void Connect(const std::string& deviceId);
    void Disconnect(const std::string& deviceId);

    // Forgets the source: its link closes and its library goes.
    void Remove(const std::string& deviceId);
    void Ping(const std::string& deviceId);
    void RefreshLibrary(const std::string& deviceId);

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
    void OnPaired(const PairedPeer& peer);

    void OnMessage(const std::string& peerId, const std::string& text);
    void OnFrame(const std::string& peerId, const std::string& bytes);
    void OnLinkUp(const std::string& peerId, std::shared_ptr<LinkTransport> transport);
    void OnLinkDown(const std::string& peerId);
    void FailForwarding();

    // Any thread.
    void OnLibraryChanged();
    [[nodiscard]] RemotePlayback::Callbacks PlaybackCallbacks();
    void Notice(const std::string& code, const std::string& peerId, const std::string& reason = {},
        const std::vector<std::string>& items = {});

    SessionThread& m_thread;
    ModuleCallbacks m_callbacks;

    // Made by Load.
    std::unique_ptr<PeerStore> m_sources;
    std::unique_ptr<PairingManager> m_pairing;
    std::unique_ptr<PeerConnections> m_connections;

    mutable std::mutex m_handlerMutex;
    std::function<void()> m_libraryHandler;

    // One per Ready link; read from the HTTP server's threads.
    mutable std::mutex m_forwardingMutex;
    std::map<std::string, std::shared_ptr<ForwardingClient>> m_forwarding;

    RemoteLibraries m_libraries;
    RemotePlayback m_playback;
};

}  // namespace mango::remote
