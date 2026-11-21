#pragma once

#include <optional>
#include <string>

#include "../RemoteModule.hpp"

namespace mango::remote
{

// An "error" from the service, for the module that sent requestId.
struct SignalError
{
    std::string requestId;
    std::string code;
};

// std::nullopt when text isn't an error with a request ID.
[[nodiscard]] std::optional<SignalError> ReadSignalError(const std::string& text);

// Hands the one signaling socket's messages to the host or the client
// module (see docs/scope/two-way-remote-scope.md). The message kind says
// which: only a client offers and only a host answers, and a pairing
// proof names its sender's side. Everything else goes to both, which
// ignore what isn't theirs. Nothing here needs the service to change.
//
// Lives on the remote thread.
class SignalingRouter
{
   public:
    SignalingRouter(SignalingModule& host, SignalingModule& client);

    void OnReady();
    void OnLost();
    void OnMessage(const std::string& type, const std::string& text);

   private:
    void OnSignal(const std::string& text);
    void RoutePair(const std::string& from, const std::string& payloadText);
    static void LogError(const std::string& text);

    SignalingModule& m_host;
    SignalingModule& m_client;
};

}  // namespace mango::remote
