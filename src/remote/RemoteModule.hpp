#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include "RemoteState.hpp"
#include "identity/DeviceIdentity.hpp"
#include "signaling/SignalingClient.hpp"

namespace mango::remote
{

// One side of remote streaming (RemoteHostModule, RemoteClientModule) as
// the SignalingRouter sees it. Each module ignores what isn't its own.
class SignalingModule
{
   public:
    virtual ~SignalingModule() = default;

    virtual void OnSignalingReady() = 0;
    virtual void OnSignalingLost() = 0;

    // invite.* and error messages, given to both modules: each handles only
    // its own invite and request IDs.
    virtual void HandleServiceMessage(const std::string& type, const std::string& text) = 0;

    // A "signal" whose payload is an offer (to the host) or an answer (to
    // the client).
    virtual void HandleSdpSignal(const std::string& from, const std::string& payloadText) = 0;

    // A "signal" whose payload is a pairing proof meant for this module.
    virtual void HandlePairSignal(const std::string& from, const std::string& payloadText) = 0;
};

// What a module gets once this device's key is loaded. Both modules share
// the identity and the signaling socket, nothing else.
struct ModuleSetup
{
    const DeviceIdentity& identity;
    SignalingClient& signaling;
    // The module's own list: clients.json or sources.json.
    std::filesystem::path storePath;
    std::string deviceName;
    std::string appVersion;
};

// How a module tells RemoteCore about changes. RemoteCore numbers the
// notices.
struct ModuleCallbacks
{
    std::function<void()> onChanged;
    std::function<void(RemoteNotice notice)> onNotice;
    std::function<void(const std::string& deviceId)> onPaired;
};

}  // namespace mango::remote
