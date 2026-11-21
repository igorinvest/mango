#include "SdpEnvelope.hpp"

#include <chrono>
#include <cstdlib>
#include <format>

namespace mango::remote::SdpEnvelope
{

namespace
{

// Every signed field, each on its own line; the SDP last, since it has
// line breaks of its own.
std::string SignedText(const SdpPayload& payload)
{
    return std::format("mango-sdp-v1\n{}\n{}\n{}\n{}\n{}\n{}", payload.kind, payload.from, payload.to,
        payload.sessionId, payload.ts, payload.sdp);
}

}  // namespace

SdpPayload Make(const DeviceIdentity& identity, std::string kind, std::string sdp, std::string recipient,
    std::string sessionId, std::int64_t nowUnixSeconds)
{
    SdpPayload payload{.kind = std::move(kind),
        .sdp = std::move(sdp),
        .from = identity.DeviceId(),
        .to = std::move(recipient),
        .sessionId = std::move(sessionId),
        .ts = nowUnixSeconds,
        .signature = {}};
    payload.signature = identity.Sign(SignedText(payload));
    return payload;
}

bool Verify(const SdpPayload& payload, const std::string& publicKey, const std::string& recipient,
    std::int64_t nowUnixSeconds)
{
    if (payload.to != recipient || DeviceIdentity::DeviceIdOf(publicKey) != payload.from)
    {
        return false;
    }
    if (std::llabs(nowUnixSeconds - payload.ts) > kMaxAgeSeconds)
    {
        return false;
    }
    if (payload.kind != "offer" && payload.kind != "answer")
    {
        return false;
    }
    return DeviceIdentity::Verify(publicKey, SignedText(payload), payload.signature);
}

std::int64_t NowUnixSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace mango::remote::SdpEnvelope
