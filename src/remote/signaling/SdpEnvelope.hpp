#pragma once

#include <cstdint>
#include <string>

#include <glaze/glaze.hpp>

#include "../identity/DeviceIdentity.hpp"

namespace mango::remote
{

// The payloads two Mango apps send each other through the service's
// "signal" message. kind says which one it is. Their keys are spelled out
// in glz::meta at the end of this file: they're protocol between two Mango
// versions, so they're only ever added, never renamed.
struct SignalKind
{
    std::string kind;
};

// Pairing: the sender's proof that it holds the invite secret, and the name
// it wants to be shown under. side is the sender's: "inviter" (the
// receiver's client module checks it) or "joiner" (its host module does).
// It isn't signed, but the proof is bound to the side, so a changed one
// only fails the pairing.
struct PairPayload
{
    std::string kind = "pair";
    std::string proof;
    std::string name;
    std::string side;
};

// An offer or answer. The SDP holds the DTLS fingerprint the WebRTC
// encryption is bound to, so it's signed with the sender's device key,
// together with who it's from and to and a fresh session ID and time, so
// a captured one can't be replayed or redirected.
struct SdpPayload
{
    std::string kind;  // "offer" or "answer"
    std::string sdp;
    std::string from;
    std::string to;
    std::string sessionId;
    std::int64_t ts = 0;  // Unix seconds
    std::string signature;
};

namespace SdpEnvelope
{

// An offer or answer from identity to the device recipient, signed.
[[nodiscard]] SdpPayload Make(const DeviceIdentity& identity, std::string kind, std::string sdp,
    std::string recipient, std::string sessionId, std::int64_t nowUnixSeconds);

// Whether payload was signed by publicKey, is addressed to recipient, and
// was made within kMaxAgeSeconds of now.
[[nodiscard]] bool Verify(const SdpPayload& payload, const std::string& publicKey, const std::string& recipient,
    std::int64_t nowUnixSeconds);

inline constexpr std::int64_t kMaxAgeSeconds = 300;

[[nodiscard]] std::int64_t NowUnixSeconds();

}  // namespace SdpEnvelope

}  // namespace mango::remote

template <>
struct glz::meta<mango::remote::SignalKind>
{
    using T = mango::remote::SignalKind;
    static constexpr auto value = object("kind", &T::kind);
};

template <>
struct glz::meta<mango::remote::PairPayload>
{
    using T = mango::remote::PairPayload;
    static constexpr auto value = object("kind", &T::kind, "proof", &T::proof, "name", &T::name, "side", &T::side);
};

template <>
struct glz::meta<mango::remote::SdpPayload>
{
    using T = mango::remote::SdpPayload;
    static constexpr auto value = object("kind", &T::kind, "sdp", &T::sdp, "from", &T::from, "to", &T::to,
        "sessionId", &T::sessionId, "ts", &T::ts, "signature", &T::signature);
};
