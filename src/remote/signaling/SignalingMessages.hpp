#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <glaze/glaze.hpp>

// The signaling service's JSON messages (signal/README.md).
// Incoming ones are read ignoring keys not listed here, so the service can
// add fields. Every key is spelled out in a glz::meta at the end, so
// renaming a member can't change the protocol.
namespace mango::remote::signaling
{

// Every incoming message, enough to dispatch it. Also reads the data
// channel's control messages, whose "requestId" is a number: so only type.
struct Envelope
{
    std::string type;
};

struct Challenge
{
    std::string nonce;
};

struct Auth
{
    std::string type = "auth";
    std::string publicKey;
    std::string signature;
};

struct Ready
{
    std::string deviceId;
};

struct Ping
{
    std::string type = "ping";
};

// requestId repeats the failed request's. legacyId: the same from a service
// older than "requestId" (it said "id"); read with RequestIdOf.
struct Error
{
    std::string code;
    std::optional<std::string> requestId;
    std::optional<std::string> legacyId;
    std::optional<std::string> message;
};

[[nodiscard]] inline std::optional<std::string> RequestIdOf(const Error& error)
{
    return error.requestId ? error.requestId : error.legacyId;
}

struct PeerKey
{
    std::string deviceId;
    std::string publicKey;
};

struct InviteOpen
{
    std::string type = "invite.open";
    std::string inviteHash;
    int ttl = 600;
    std::optional<std::string> requestId;
};

struct InviteOpened
{
    std::string inviteHash;
    std::int64_t expiresAt = 0;
};

struct InviteJoin
{
    std::string type = "invite.join";
    std::string inviteSecret;
    std::optional<std::string> requestId;
};

// invite.joined (to A) and invite.accepted (to B).
struct InvitePeer
{
    std::string inviteHash;
    PeerKey peer;
};

struct InviteClose
{
    std::string type = "invite.close";
    std::string inviteHash;
};

struct SignalOut
{
    std::string type = "signal";
    std::string to;
    glz::raw_json payload;
    std::optional<std::string> requestId;
};

struct SignalIn
{
    std::string from;
    glz::raw_json payload;
};

inline constexpr glz::opts kLenient{.error_on_unknown_keys = false};

// std::nullopt when text isn't a T.
template <typename T>
[[nodiscard]] std::optional<T> Read(const std::string& text)
{
    T value{};
    if (glz::read<kLenient>(value, text))
    {
        return std::nullopt;
    }
    return value;
}

template <typename T>
[[nodiscard]] std::string Write(const T& value)
{
    auto json = glz::write_json(value);
    return json ? std::move(*json) : std::string();
}

}  // namespace mango::remote::signaling

// The service's keys, spelled out (see the top of this file).
template <>
struct glz::meta<mango::remote::signaling::Envelope>
{
    using T = mango::remote::signaling::Envelope;
    static constexpr auto value = object("type", &T::type);
};

template <>
struct glz::meta<mango::remote::signaling::Challenge>
{
    using T = mango::remote::signaling::Challenge;
    static constexpr auto value = object("nonce", &T::nonce);
};

template <>
struct glz::meta<mango::remote::signaling::Auth>
{
    using T = mango::remote::signaling::Auth;
    static constexpr auto value = object("type", &T::type, "publicKey", &T::publicKey, "signature", &T::signature);
};

template <>
struct glz::meta<mango::remote::signaling::Ready>
{
    using T = mango::remote::signaling::Ready;
    static constexpr auto value = object("deviceId", &T::deviceId);
};

template <>
struct glz::meta<mango::remote::signaling::Ping>
{
    using T = mango::remote::signaling::Ping;
    static constexpr auto value = object("type", &T::type);
};

template <>
struct glz::meta<mango::remote::signaling::Error>
{
    using T = mango::remote::signaling::Error;
    static constexpr auto value = object(
        "code", &T::code, "requestId", &T::requestId, "id", &T::legacyId, "message", &T::message);
};

template <>
struct glz::meta<mango::remote::signaling::PeerKey>
{
    using T = mango::remote::signaling::PeerKey;
    static constexpr auto value = object("deviceId", &T::deviceId, "publicKey", &T::publicKey);
};

template <>
struct glz::meta<mango::remote::signaling::InviteOpen>
{
    using T = mango::remote::signaling::InviteOpen;
    static constexpr auto value =
        object("type", &T::type, "inviteHash", &T::inviteHash, "ttl", &T::ttl, "requestId", &T::requestId);
};

template <>
struct glz::meta<mango::remote::signaling::InviteOpened>
{
    using T = mango::remote::signaling::InviteOpened;
    static constexpr auto value = object("inviteHash", &T::inviteHash, "expiresAt", &T::expiresAt);
};

template <>
struct glz::meta<mango::remote::signaling::InviteJoin>
{
    using T = mango::remote::signaling::InviteJoin;
    static constexpr auto value = object("type", &T::type, "inviteSecret", &T::inviteSecret, "requestId",
        &T::requestId);
};

template <>
struct glz::meta<mango::remote::signaling::InvitePeer>
{
    using T = mango::remote::signaling::InvitePeer;
    static constexpr auto value = object("inviteHash", &T::inviteHash, "peer", &T::peer);
};

template <>
struct glz::meta<mango::remote::signaling::InviteClose>
{
    using T = mango::remote::signaling::InviteClose;
    static constexpr auto value = object("type", &T::type, "inviteHash", &T::inviteHash);
};

template <>
struct glz::meta<mango::remote::signaling::SignalOut>
{
    using T = mango::remote::signaling::SignalOut;
    static constexpr auto value = object("type", &T::type, "to", &T::to, "payload", &T::payload,
        "requestId", &T::requestId);
};

template <>
struct glz::meta<mango::remote::signaling::SignalIn>
{
    using T = mango::remote::signaling::SignalIn;
    static constexpr auto value = object("from", &T::from, "payload", &T::payload);
};
