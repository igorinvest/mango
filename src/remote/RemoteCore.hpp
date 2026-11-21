#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "../session/SessionThread.hpp"
#include "RemoteClientModule.hpp"
#include "RemoteHostModule.hpp"
#include "RemoteState.hpp"
#include "RemoteStreaming.hpp"
#include "identity/DeviceIdentity.hpp"
#include "signaling/SignalingClient.hpp"
#include "signaling/SignalingRouter.hpp"

namespace mango::remote
{

// RemoteStreaming's insides: this device's identity, the one signaling
// socket and its router, and the two modules that don't share anything
// else: the host (clients play this device's library) and the client (this
// device plays its sources' libraries). See
// docs/scope/two-way-remote-scope.md.
//
// Everything but the snapshot, the handler and the "Streaming" methods
// lives on m_thread; the public methods post there. The modules live as
// long as this, so the engine's threads can use them without racing
// Shutdown.
class RemoteCore
{
   public:
    explicit RemoteCore(RemoteOptions options);
    ~RemoteCore();

    RemoteCore(const RemoteCore&) = delete;
    RemoteCore& operator=(const RemoteCore&) = delete;

    void Start();
    void Shutdown();

    void SetChangedHandler(std::function<void()> handler);
    void SetNoticeHandler(std::function<void(const RemoteNotice&)> handler);
    [[nodiscard]] std::string GetStateJson() const;

    void SetAppVersion(const std::string& version);
    void SetEnabled(bool enabled);
    void SetStreamingAllowed(bool allowed);
    void SetDeviceName(const std::string& name);

    // Host: invites and clients.
    void OpenInvite();
    void CancelHostPairing();
    void RemoveClient(const std::string& deviceId);
    void PingClient(const std::string& deviceId);

    // Client: joins and sources.
    bool JoinInvite(const std::string& link);
    void CancelClientPairing();
    void ConnectSource(const std::string& deviceId);
    void DisconnectSource(const std::string& deviceId);
    void RemoveSource(const std::string& deviceId);
    void PingSource(const std::string& deviceId);
    void RefreshLibrary(const std::string& deviceId);

    // Streaming, any thread.

    // Host: the engine, which serves this device's library. Before Start.
    void SetHost(std::shared_ptr<RemoteHost> host) { m_host.SetHost(std::move(host)); }

    // Client: the sources' libraries, forwarding and playback.
    void SetLibraryChangedHandler(std::function<void()> handler)
    {
        m_client.SetLibraryChangedHandler(std::move(handler));
    }
    [[nodiscard]] std::vector<LibraryEntry> GetLibraryEntries() const { return m_client.GetLibraryEntries(); }
    [[nodiscard]] std::optional<LibraryEntry> FindLibraryEntry(const std::string& entryId) const
    {
        return m_client.FindLibraryEntry(entryId);
    }
    [[nodiscard]] std::shared_ptr<ForwardingClient> FindClient(const std::string& deviceId) const
    {
        return m_client.FindClient(deviceId);
    }
    [[nodiscard]] RemotePlayback& Playback() { return m_client.Playback(); }

   private:
    // On the thread.
    void Load();
    void CreateSignaling();
    void LoadModules();
    void ApplyEnabled(bool enabled);
    void ApplyStreamingAllowed(bool allowed);
    void Enable();
    void OnHostPaired(const std::string& deviceId);

    [[nodiscard]] ModuleCallbacks HostCallbacks();
    [[nodiscard]] ModuleCallbacks ClientCallbacks();
    void ShowNotice(RemoteNotice notice);
    void Publish();
    [[nodiscard]] RemoteState BuildState() const;

    void Post(std::function<void()> task);

    const RemoteOptions m_options;
    SessionThread m_thread;
    bool m_started = false;
    bool m_shutDown = false;

    RemoteHostModule m_host;
    RemoteClientModule m_client;
    SignalingRouter m_router;

    // Made by Load; null when this device's key can't be made.
    std::optional<DeviceIdentity> m_identity;
    std::unique_ptr<SignalingClient> m_signaling;

    bool m_enabled = false;
    bool m_streamingAllowed = true;
    std::string m_deviceName;
    std::string m_appVersion;
    RemoteNotice m_notice;

    mutable std::mutex m_stateMutex;
    std::string m_stateJson;
    std::function<void()> m_handler;
    std::function<void(const RemoteNotice&)> m_noticeHandler;
};

}  // namespace mango::remote
