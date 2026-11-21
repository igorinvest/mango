#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include "RemoteState.hpp"

namespace mango
{

namespace remote
{
class RemoteCore;
}

// Where RemoteStreaming keeps its files and which service it talks to;
// tests change them.
struct RemoteOptions
{
    // identity.json, clients.json and sources.json. Empty: <app data>/remote.
    std::filesystem::path folder;
    // The signaling service (wss://…/v1). Empty: "remoteSignalingUrl" in
    // settings.json, else the build's MANGO_SIGNALING_URL, when useSettings;
    // else none (stays disconnected).
    std::string signalingUrl;
    // Read and write "remoteStreamingEnabled", "remoteStreamingAllowed" and
    // "remoteDeviceName" in settings.json. Off: starts disabled, named after the host.
    bool useSettings = true;
};

// Remote streaming between two Mango apps (see
// docs/scope/hybrid_streaming_architecture.md): this device's identity,
// pairing through the signaling service, an encrypted WebRTC data channel
// to each paired device opened with the hello handshake, and over it HTTP
// forwarding, the sources' libraries and remote playback. Owned by Engine
// (Engine::GetRemote).
//
// Each device has two independent sides (see
// docs/scope/two-way-remote-scope.md): as a host, its clients play its
// library; as a client, it plays its sources' libraries. Two devices can
// be each other's client and source at once, with a link per direction.
//
// Playing a remote video needs nothing from this class: its library entry
// (see GetLibraryEntriesJson) goes through Engine's and Session's usual
// calls, which start it on the peer and cast B's /remote/... URL.
//
// Commands return immediately; the work happens on its own thread. The app
// learns about changes from the handler and reads GetStateJson (a
// RemoteState).
class RemoteStreaming
{
   public:
    explicit RemoteStreaming(RemoteOptions options = {});
    ~RemoteStreaming();

    RemoteStreaming(const RemoteStreaming&) = delete;
    RemoteStreaming& operator=(const RemoteStreaming&) = delete;

    // Loads the device key and paired peers (returns once they're loaded),
    // and connects to the service if remote streaming is on. A second call
    // does nothing.
    void Start();

    // Closes every link and the service's socket. Commands are ignored
    // afterwards.
    void Shutdown();

    // Called on the remote thread after each change. Keep it short.
    void SetChangedHandler(std::function<void()> handler);

    // Called on the remote thread with each new notice (also kept in the
    // state's notice). The session uses it to add remote notices to its own
    // list (see Session's "notice" part). Keep it short.
    void SetNoticeHandler(std::function<void(const RemoteNotice&)> handler);

    // A RemoteState as JSON. Any thread.
    [[nodiscard]] std::string GetStateJson() const;

    // The app's version, sent in hello. Call before Start.
    void SetAppVersion(const std::string& version);

    // Saved in settings.json ("remoteStreamingEnabled"). On: stays
    // connected to the service so paired devices can reach this one. Off:
    // closes everything.
    void SetEnabled(bool enabled);

    // Saved in settings.json ("remoteStreamingAllowed", default on). Off:
    // clients' connections to this one are refused and open ones close;
    // pairing, the service and this device's links to its sources keep
    // working. A new client's pairing turns it back on.
    void SetStreamingAllowed(bool allowed);

    // The name paired devices show this one under, sent when pairing.
    // Saved in settings.json ("remoteDeviceName"); default: the host name.
    void SetDeviceName(const std::string& name);

    // ---- Host: devices that play this one's library ("clients")

    // Opens an invite; its link appears in the state's hostPairing once the
    // service accepted it. The device that joins becomes a client. Turns
    // remote on.
    void OpenInvite();
    void CancelHostPairing();

    // Forgets the client: its link closes and its signals are ignored from
    // now on. The same device stays a source if it is one.
    void RemoveClient(const std::string& deviceId);

    // Sends a ping over the data channel; the round trip shows up as the
    // device's pingMs.
    void PingClient(const std::string& deviceId);

    // ---- Client: devices whose libraries this one plays ("sources")

    // Joins the invite behind link ("mango://pair?…"); the device that
    // showed it becomes a source. false when link isn't one. Turns remote
    // on.
    bool JoinInvite(const std::string& link);
    void CancelClientPairing();

    // This device connects to its sources by itself; these are for a UI's
    // Connect/Disconnect and for tests.
    void ConnectSource(const std::string& deviceId);
    void DisconnectSource(const std::string& deviceId);

    // Forgets the source: its link closes and its library goes. The same
    // device stays a client if it is one.
    void RemoveSource(const std::string& deviceId);
    void PingSource(const std::string& deviceId);

    // Every source's library, as a JSON array of LibraryEntry with
    // remotePeerId set (see LibraryEntry). Fetched when the source
    // connects; it stays listed while its link is down. Any thread.
    [[nodiscard]] std::string GetLibraryEntriesJson() const;

    // Fetches the source's library again (after it added videos).
    void RefreshLibrary(const std::string& deviceId);

    // Called on a worker thread after a source's library changed.
    void SetLibraryChangedHandler(std::function<void()> handler);

    // The engine's way in (internal: Engine wires the HTTP server and the
    // library to it).
    [[nodiscard]] remote::RemoteCore& Core() { return *m_core; }

   private:
    std::unique_ptr<remote::RemoteCore> m_core;
};

}  // namespace mango
