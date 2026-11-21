#pragma once

#include <optional>
#include <string>
#include <vector>

#include <glaze/glaze.hpp>

#include "../../file/Serialization.hpp"  // VideoCodecConfig, AudioCodecConfig
#include "../../pipeline/stream/profiles/StreamProfile.hpp"

// The first message each side sends when the data channel opens (see
// "Version compatibility" in docs/scope/hybrid_streaming_architecture.md).
// Every key is spelled out in glz::meta at the end of this file: fields are
// only ever added, never renamed.
namespace mango::remote
{

// Raised only for a change that truly breaks the format.
inline constexpr int kProtocolVersion = 1;

struct InitiateEncoding
{
    std::string videoBitrate;
    std::string audioBitrate;
    std::string videoPixFmt;
    std::string audioCodec;
};

struct InitiateTarget
{
    std::string playlist;
    std::string preset;
    std::string mode;
    std::vector<VideoCodecConfig> videoCapabilities;
    std::vector<AudioCodecConfig> audioCapabilities;
    bool rotationSupported = false;
    InitiateEncoding encoding;
};

// The values A takes for each field with a fixed set.
struct InitiateAccepts
{
    std::vector<std::string> playlist;
    std::vector<std::string> preset;
    std::vector<std::string> mode;
    std::vector<std::string> videoCodecs;
    std::vector<std::string> audioCodecs;
    std::vector<std::string> videoPixFmt;
    std::vector<std::string> audioCodec;
};

// A's example of an initiate request: every field it knows, with its
// built-in defaults from the code — never the user's settings.
struct InitiateExample
{
    InitiateTarget target;
    InitiateAccepts accepts;
};

struct HelloMessage
{
    std::string type = "hello";
    int protocol = kProtocolVersion;
    std::string appVersion;
    InitiateExample initiateExample;
};

// A peer's hello, as far as this version reads it. The example is kept as
// it came: B builds its initiate requests from it (M3).
struct PeerHello
{
    int protocol = 0;
    std::string appVersion;
    glz::raw_json initiateExample;
};

enum class Compatibility
{
    Compatible,
    // The peer's protocol is older: it must update.
    PeerNeedsUpdate,
    // The peer's protocol is newer: this app must update.
    UpdateRequired,
};

namespace Hello
{

[[nodiscard]] InitiateExample BuiltInInitiateExample();

// This app's hello as JSON text.
[[nodiscard]] std::string Make(const std::string& appVersion);

// std::nullopt when text isn't a hello.
[[nodiscard]] std::optional<PeerHello> Read(const std::string& text);

[[nodiscard]] Compatibility Check(int peerProtocol);

}  // namespace Hello

}  // namespace mango::remote

template <>
struct glz::meta<mango::remote::InitiateEncoding>
{
    using T = mango::remote::InitiateEncoding;
    static constexpr auto value = object("videoBitrate", &T::videoBitrate, "audioBitrate", &T::audioBitrate,
        "videoPixFmt", &T::videoPixFmt, "audioCodec", &T::audioCodec);
};

template <>
struct glz::meta<mango::remote::InitiateTarget>
{
    using T = mango::remote::InitiateTarget;
    static constexpr auto value = object("playlist", &T::playlist, "preset", &T::preset, "mode", &T::mode,
        "videoCapabilities", &T::videoCapabilities, "audioCapabilities", &T::audioCapabilities,
        "rotationSupported", &T::rotationSupported, "encoding", &T::encoding);
};

template <>
struct glz::meta<mango::remote::InitiateAccepts>
{
    using T = mango::remote::InitiateAccepts;
    static constexpr auto value = object("playlist", &T::playlist, "preset", &T::preset, "mode", &T::mode,
        "videoCodecs", &T::videoCodecs, "audioCodecs", &T::audioCodecs, "videoPixFmt", &T::videoPixFmt,
        "audioCodec", &T::audioCodec);
};

template <>
struct glz::meta<mango::remote::InitiateExample>
{
    using T = mango::remote::InitiateExample;
    static constexpr auto value = object("target", &T::target, "accepts", &T::accepts);
};

template <>
struct glz::meta<mango::remote::HelloMessage>
{
    using T = mango::remote::HelloMessage;
    static constexpr auto value = object("type", &T::type, "protocol", &T::protocol, "appVersion",
        &T::appVersion, "initiateExample", &T::initiateExample);
};

template <>
struct glz::meta<mango::remote::PeerHello>
{
    using T = mango::remote::PeerHello;
    static constexpr auto value =
        object("protocol", &T::protocol, "appVersion", &T::appVersion, "initiateExample", &T::initiateExample);
};
