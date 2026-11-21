#include "SignalingRouter.hpp"

#include "../../log/Log.hpp"
#include "SdpEnvelope.hpp"
#include "SignalingMessages.hpp"

namespace mango::remote
{

std::optional<SignalError> ReadSignalError(const std::string& text)
{
    const auto error = signaling::Read<signaling::Error>(text);
    const auto requestId = error ? signaling::RequestIdOf(*error) : std::nullopt;
    if (!requestId)
    {
        return std::nullopt;
    }
    return SignalError{.requestId = *requestId, .code = error->code};
}

SignalingRouter::SignalingRouter(SignalingModule& host, SignalingModule& client) : m_host(host), m_client(client) {}

void SignalingRouter::OnReady()
{
    m_host.OnSignalingReady();
    m_client.OnSignalingReady();
}

void SignalingRouter::OnLost()
{
    m_host.OnSignalingLost();
    m_client.OnSignalingLost();
}

void SignalingRouter::OnMessage(const std::string& type, const std::string& text)
{
    if (type == "signal")
    {
        OnSignal(text);
        return;
    }
    if (type == "error")
    {
        LogError(text);
    }
    m_host.HandleServiceMessage(type, text);
    m_client.HandleServiceMessage(type, text);
}

void SignalingRouter::OnSignal(const std::string& text)
{
    const auto signal = signaling::Read<signaling::SignalIn>(text);
    const auto kind = signal ? signaling::Read<SignalKind>(signal->payload.str) : std::nullopt;
    if (!kind)
    {
        return;
    }

    if (kind->kind == "offer")
    {
        m_host.HandleSdpSignal(signal->from, signal->payload.str);
    }
    else if (kind->kind == "answer")
    {
        m_client.HandleSdpSignal(signal->from, signal->payload.str);
    }
    else if (kind->kind == "pair")
    {
        RoutePair(signal->from, signal->payload.str);
    }
}

// The inviter's proof is for the joiner (this device's client module), and
// the other way round.
void SignalingRouter::RoutePair(const std::string& from, const std::string& payloadText)
{
    const auto payload = signaling::Read<PairPayload>(payloadText);
    if (payload && payload->side == "inviter")
    {
        m_client.HandlePairSignal(from, payloadText);
    }
    else if (payload && payload->side == "joiner")
    {
        m_host.HandlePairSignal(from, payloadText);
    }
    else
    {
        Log::Debug(LogCategory::Remote, "ignored a pairing proof without a side from {}", from.substr(0, 8));
    }
}

void SignalingRouter::LogError(const std::string& text)
{
    const auto error = signaling::Read<signaling::Error>(text);
    if (error)
    {
        Log::Warning(LogCategory::Remote, "signaling error {}{}", error->code,
            error->message ? ": " + *error->message : std::string());
    }
}

}  // namespace mango::remote
