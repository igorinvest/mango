#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mango
{

// One paired device in RemoteState's clients (it plays this device's
// library on its TV) or sources (this device plays its library). The same
// device can be in both, with a link of its own in each. link:
// "disconnected", "connecting", "handshaking",
// "connected", "failed", "offline" (the peer's app isn't running),
// "peerNeedsUpdate" or "updateRequired" (incompatible versions: show
// "<name> needs an update" or "Update Mango"). peerAppVersion is from the
// peer's last hello; pingMs the last `ping` round trip.
struct RemotePeerState
{
    std::string deviceId;
    std::string name;
    std::int64_t pairedAtUnixSeconds = 0;
    std::string link = "disconnected";
    std::string peerAppVersion;
    std::optional<std::int64_t> pingMs;

    // Sources only: the peer's library on this device, "none" (not
    // fetched yet), "loading", "ready" or "failed"; libraryCount its videos.
    std::string library = "none";
    int libraryCount = 0;
};

// A pairing in progress. The host's stage: "idle", "opening",
// "waitingForPeer" (show link as a QR code and as text until
// expiresAtUnixSeconds), "confirming". The client's: "idle", "joining",
// "confirming".
struct RemotePairingState
{
    std::string stage = "idle";
    std::string link;
    std::int64_t expiresAtUnixSeconds = 0;
};

// The latest message for the user (see "Notices" in
// docs/scope/hybrid_streaming_architecture.md). noticeId increases with each, so
// an app shows each one once. Codes: "paired" (peer), "pairingFailed"
// (reason), "peerConnectionFailed" (peer), "settingsNotApplied" (peer,
// items), "remoteStreamStarted" (peer, file), "streamStoppedByPeer" (peer,
// file). peer is the peer's name; items are
// codecs ("hevc"), presets ("4k") or field names the peer's older Mango
// didn't take.
struct RemoteNotice
{
    std::uint64_t noticeId = 0;
    std::string code;
    std::string peer;
    std::string reason;
    std::string file;
    std::vector<std::string> items;
};

// Everything RemoteStreaming::GetStateJson returns. signaling: "off",
// "connecting" or "ready". signalingConfigured is false when there's no
// signaling service URL (neither "remoteSignalingUrl" in settings.json nor
// the build's default), so signaling stays "off" and pairing can't work.
// available is false when this device's key
// couldn't be made (nothing remote works then). streamingAllowed false:
// clients can't connect to this one (see
// RemoteStreaming::SetStreamingAllowed). clients play this device's
// library; sources are the devices whose libraries this one plays.
struct RemoteState
{
    bool enabled = false;
    bool streamingAllowed = true;
    bool available = true;
    std::string signaling = "off";
    bool signalingConfigured = true;
    std::string deviceId;
    std::string deviceName;
    int protocol = 0;
    RemotePairingState hostPairing;
    RemotePairingState clientPairing;
    std::vector<RemotePeerState> clients;
    std::vector<RemotePeerState> sources;
    RemoteNotice notice;
};

}  // namespace mango
